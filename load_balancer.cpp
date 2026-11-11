// ======================= TIME-TRAVEL DEBUGGER - LOAD BALANCER TEMPLATE =======================

// What load balancer does:
//   1. Many clients can connect at the same time. Each one sends one string (the source code). 
//   2. Clients wait in a queue (first come, first served).
//   3. For each client, in order: make sure the server is running (start it with system() if not),
//      pass the string to the server, pass the server's reply back to the client.
//
// It does not read or understand the messages. It only moves bytes.
// The only messages it writes itself are errors ("server busy", "server failed to start").
//
// Two threads:
//   acceptor thread (acceptLoop) -> takes new clients and puts them in the queue. It never waits for the server.
//   main thread (dispatcherLoop) -> takes ONE client out of the queue and serves it completely, then the next.
//
// How the server is started:
//   system("./server 5001 &");
//   The "&" is required. Without it, system() waits until the server exits and the load balancer freezes.
//
//
// What the server program must do (the load balancer depends on this):
//   - it takes its port as the first argument, for example: ./server 5001
//   - it reads the string until the connection's write side is closed (recv returns 0)
//   - it sends its reply as [status: 1 byte][payload], then closes the connection 


#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helper.h"
using namespace std;


// ---- Constants ----
const char *SERVER_PATH = "./server";      // the program we start when the server is down
const int IO_BUFFER_SIZE = 64 * 1024;      // fixed buffer for moving bytes
const int LISTEN_BACKLOG = 16;             // connections the OS holds until accept() takes them
const int MAX_PENDING_CLIENTS = 8;         // clients allowed to wait in our queue. More than this get "server busy".
const int STARTUP_MAX_ATTEMPTS = 50;       // after starting the server, how many times we try to connect
const int STARTUP_POLL_INTERVAL_MS = 100;  // wait between those tries (50 x 100 ms = 5 seconds in total)
int lbPort = 5000;
string serverHost = "127.0.0.1";
int serverPort = 5001;


// ---- Queue: here it holds client sockets (int). ----
// Implement it yourself, do not use std::queue.
template <typename T>
class Queue
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *front, *rear;
    int count;


public:
    // Implement these functions:
    Queue()
    {
        front=rear=nullptr;
        count=0;

    }
    ~Queue()
    {
        if(front==rear){
            delete front;
        }
        else{
            while(front!=nullptr){
                Node* temp=front;
                front=front->next;
                delete temp;
            }
        }

    }
    void enqueue(const T &val)
    {
        if(front==nullptr){
            front=new Node;
            front->data=val;
            front->next=nullptr;
            rear=front;
            count++;
        }
        else{
            Node* temp=new Node;
            temp->data=val;
            temp->next=nullptr;
            rear->next=temp;
            rear=rear->next;
            count++;
        }
    }
    T dequeue()
    {
        if(isEmpty()){
            cout<<"ERROR! ITS EMPTY ALREADY"<<endl;
       return T{};
        }
        else if(front==rear){
             Node* temp=front;
             T val=temp->data;
            front=rear=nullptr;
            count--;
            delete temp;
            return val;
        }
        else{
            Node* temp=front;
            T val=temp->data;
            front=front->next;
            count--;
            delete temp;
            return val;
        }
    }
    bool isEmpty()
    {
     return count==0;   
    }
    int size()
    {
        return count;

    }
};

struct ClientInfo
{
    int sock;           // the connected socket
    string address;     // the client's IP as text, e.g. "127.0.0.1"
    int port;           // the client's port
    time_t arrivedAt;   // when accept() returned
};

// The queue is used by two threads, so every use of it must be inside queueMutex.
Queue<ClientInfo> pendingClients;
mutex queueMutex;
sig_atomic_t flag = 1;


// ---- Basic socket helpers ----
bool sendAll(int sock, const char *buf, size_t n) 
{
    size_t check=0;
    while(true){
        ssize_t bytes=send(sock,buf+check,n-check,MSG_NOSIGNAL);
        if(bytes<0){
            if(errno==EINTR){
                continue;
            }
            else{
                return false;
            }
        }
        if(bytes==0){
            return false;
        }
        check=check+bytes;
        if(check>=n){
            break;
        }
    }
    // Reply format: [status: 1 byte][sending_material][checksum: 4 bytes].
    // send() may send fewer bytes than asked for, so keep calling it until all n bytes are sent.
      return true;
}
bool sendError(int sock, const string &message)
{
    char status=1;
    Crc32 chcksum;
    chcksum.update(&status,1);
    chcksum.update(message.data(),message.size());
    char encrypt[4];
    chcksum.put32(chcksum.value(),encrypt);
    bool flag1=sendAll(sock,&status,1);
    bool flag2=sendAll(sock,message.data(),message.size());
    if(flag2&&flag1){
        bool flag3=sendAll(sock,encrypt,4);
        if(flag3==true){
            return true;
        }
        return false;
    }
    return false;
    // Reply format: [status: 1 byte][payload until close][checksum: 4 bytes].
    // For an error: send one byte with value 1, then the message text. 
}
int createListenSocket(int port)
{ 
    int so=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(so<0){
        perror("socket");
        exit(1);
    }
    int yes=1;
    setsockopt(so,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_port=htons(port);
    address.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(so,(sockaddr* )&address,sizeof(address))<0){
        perror("bind");
        exit(1);
    }
    if(listen(so,LISTEN_BACKLOG)<0){
        perror("listen");
        exit(1);
    }
    return so;
    // Create a TCP socket with SOCK_CLOEXEC(flag).
    // set SO_REUSEADDR so we can restart the load balancer right away.
    // bind() to the port on any address, then listen(LISTEN_BACKLOG).
    // If bind or listen fails, print the error and exit, because without this socket we cannot work.
}
int connectToServer(const char *host, int port)
{
    int so=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(so<0){
        return -1;
    }
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_port=htons(port);
    if(inet_pton(AF_INET,host,&address.sin_addr)!=1){
        close(so);
        return -1;
    }
    if(connect(so,(sockaddr*)&address,sizeof(address))<0){
        close(so);
        return -1;
    }
    return so;
    // Create a TCP socket (SOCK_CLOEXEC) and connect it to host:port.
    // Return the socket, or -1 if the connection fails 
}

// =============================================================================================
// PART 1: WAKING UP THE SERVER IF IT IS DOWN
// =============================================================================================


bool startServer(int port)
{
    string command=string(SERVER_PATH) + " " + to_string(port)+ "&";
    int ans=system(command.c_str());
    if(ans==0){
        return true;
    }
    return false;
    // Build the command text: SERVER_PATH + " " + port as text + "&"
    //   Example: "./server 5001 &"
    // Call system() with it.
    // Return true if system() returned 0, otherwise false.
}
int connectToServerWithWakeup()
{
    int ch=0;
    for(int i=0;i<=STARTUP_MAX_ATTEMPTS;i++){
        int so=connectToServer(serverHost.c_str(),serverPort);
        if(so>=0){
            return so;
        }
        else{
            if(ch==0){
                if(startServer(serverPort)==false){
                    return -1;
                }
                else{
                    ch=1;
                }
            }
        }
        usleep(STARTUP_POLL_INTERVAL_MS*1000);
    }
    return -1;
    // 1. Try connectToServer(). If it works, return that socket. The server is alive.
    // 2. Otherwise call startServer(). If it fails, return -1.
    // 3. Try connectToServer() up to STARTUP_MAX_ATTEMPTS times 
    // 4. Return the first working socket, or -1 if the server never came up.
}


// =============================================================================================
// PART 2: PASSING THE STRING AND THE REPLY (one client, called by the main thread only)
// =============================================================================================


void relayOneClient(ClientInfo &c)
{
    int serversock=connectToServerWithWakeup();
    if(serversock<0){
        sendError(c.sock,"backend server failed to start");
        return;
    }
    datatimeoutcheck(serversock,30);  //  30 IS timeout waiting seconds
    char buffer[IO_BUFFER_SIZE];
    //client se server ki taraf sawaal
    while(true){
        ssize_t so=recv(c.sock,buffer,sizeof(buffer),0);
        if(so<0){
            if(errno==EINTR){
                if(flag==false){
                    break;
                }
                continue;
            }
            close(serversock);
            return;
        }
        if(so==0){
            break;
        }
        if(sendAll(serversock,buffer,so)==false){
            sendError(c.sock,"backend server failed to send all messages");
            close(serversock);
            return;
        }
    }
    shutdown(serversock,SHUT_WR);
    //server se wapis client ki taraf reply
    while(true){
        ssize_t so=recv(serversock,buffer,sizeof(buffer),0);
        if(so<0){
            if(errno==EINTR){
                if(flag==false){
                    break;
                }
                continue;
            }
           break;
        }
        if(so==0){
            break;
        }
        if(sendAll(c.sock,buffer,so)==false){
           break;
        }
    }
    close(serversock);
    
    // 1. serverSock = connectToServerWithWakeup().
    //    If it is -1: sendError "backend server failed to start", close clientSock, return.
    // 3. Client -> server: repeat recv() from clientSock into a buffer of IO_BUFFER_SIZE and sendAll() it to serverSock.
    //    Stop when recv() returns 0 (the client finished sending) or fails.
    // 4. Call shutdown(serverSock, SHUT_WR), so the server knows the string is complete.
    // 5. Server -> client: repeat recv() from serverSock and sendAll() to clientSock.
    //    Stop when recv() returns 0 (the server sent its whole reply and closed) or fails.
    // 6. Close both sockets.
}


// =============================================================================================
// PART 3: THE TWO THREADS
// =============================================================================================


void acceptLoop(int listenSock)
{
    while(flag){
        sockaddr_in address;
        socklen_t len=sizeof(address);
        int so=accept4(listenSock,(sockaddr*)&address,&len,SOCK_CLOEXEC);
        if(so<0){
            if(errno!=EINTR){
                if(flag==true){
                    perror("accept");
                }
               continue;   
            }
            continue;
        }
            ClientInfo client;
            client.sock=so;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,&address.sin_addr,ip,sizeof(ip));
            client.address=ip;
            client.port=ntohs(address.sin_port);
            client.arrivedAt=time(nullptr);
            bool check=true;
            {
                lock_guard<mutex> lockscreen(queueMutex);
                if(pendingClients.size()>=MAX_PENDING_CLIENTS){
                    check=false;
                }
                else{
                    pendingClients.enqueue(client);
                }
            }
            if(!check){
                sendError(so,"server busy,try again later");
                close(so);
            }
        } 

    // Repeat: accept() a new client. 
    //  If pendingClients.size() is already MAX_PENDING_CLIENTS: unlock, sendError "server busy, try again later",
    // else
    // Lock queueMutex.
    //   close the client, and go on.
    //   Otherwise enqueue the client socket.
    // Never wait for the server here, so new clients are always accepted quickly.
    // If accept() fails, print the error and keep going (do not end the thread).
    }
void dispatcherLoop()
{
    while(flag){
        bool check=true;
        ClientInfo client{};
        {
            lock_guard<mutex> lockscreen(queueMutex);
            if(pendingClients.isEmpty()==false){
                client=pendingClients.dequeue();
                check=false;
            }
        }
        if(check==true){
            usleep(20000);
            continue;
        }
        if(time(nullptr)-client.arrivedAt > 30){
            cout<<"Socket closed no reply of client"<<endl;
            close(client.sock);
            continue;
        }
        cout<<"Socket processing"<<endl;
        datatimeoutcheck(client.sock,30);
        relayOneClient(client);
        close(client.sock);
    }
    // This runs in the main thread and never ends.
    // Repeat:
    //   Lock queueMutex, take ONE client out of the queue if there is one, unlock.
    //   If we got a client: call relayOneClient(). The next client is taken out of the queue only after this one is finished.
}

void handler(int sg) {
    (void)sg;
    flag = 0;
}
int main(int argc, char *argv[])
{
    if(argc!=3){
        cout<<"You call wrong path use \"./load_balancer <server_ip> <server_port>\""<<endl;
        return 1;
    }
    serverHost=argv[1];
    serverPort=atoi(argv[2]);
    if(serverPort<1 || serverPort > 65535){
        cout<<"server port is wrong"<<endl;
        return 1;
    }
    signal(SIGINT, handler); // whenever ctrl+c will be pressed to exit the program the flag wll be set so insead of using infinite loop you can loop on flag to safely close things and exit
    int listenso=createListenSocket(lbPort);
    cout<<"Load Balancer starts listen on port"<<lbPort<<" whereas the server is at "<<serverHost<<" and serverPort is at "<<serverPort<<endl;
    thread acceptor(acceptLoop,listenso);
    acceptor.detach();
    dispatcherLoop();
    close(listenso);
    {
        lock_guard<mutex> lockscreen(queueMutex);
        while(!pendingClients.isEmpty()){
            ClientInfo client=pendingClients.dequeue();
            close(client.sock);
        }
    }
    cout<<"Load Balancer stops now...."<<endl;
    // 2. listenSock = createListenSocket(LB_PORT).
    // 3. Start a thread that runs acceptLoop(listenSock) and detach it.
    // 4. Call dispatcherLoop().
    return 0;
}