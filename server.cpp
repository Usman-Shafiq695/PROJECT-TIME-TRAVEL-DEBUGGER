// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)


#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdint>
#include <cstdio>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *top;
    int32_t count;

public:
    // Implement these functions:
    Stack()
    { 
        top=nullptr;
        count=0;
    }
    void push(const T &val)
    {

        if(MAX_STACK_DEPTH==count){
            cout<<"STACK IS FULL"<<endl;
            return;
        }
        if(count==0){
            top=new Node;
            top->data=val;
            top->next=nullptr;
            count++;
            return;
        }
        Node* temp=new Node;
        temp->next=top;
        temp->data=val;
        top=temp;
        count++;

    }
    T pop()
    {
        T ans{};
        if(top==nullptr){
            cout<<"STACK IS ALREADY EMPTY"<<endl;
            return ans;
        }
        else if(count==1){
            Node* temp=top;
            ans=temp->data;
            delete[] temp;
            top=nullptr;
            count--;
        }
        else{
            Node* temp=new Node;
            temp=top;
            ans=temp->data;
            top=top->next;
            delete[] temp;
            count--;
        }

    }
    T &peek()
    {
         T ans{};
        if(top==nullptr){
            cout<<"STACK IS ALREADY EMPTY"<<endl;
            return ans;
        }
        ans=top->data;
    }
    bool isEmpty()
    {
        return top==nullptr;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        int32_t i=0;
        Node* temp=new Node;
        temp=top;
       while(temp!=nullptr && i<maxLen){
        out[i]=temp->data;
        i++;
        temp=temp->next;
       }
       return i;
    }
};


// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    // Implement these functions
    Timeline()
    {
        head=tail=nullptr;
        stepCount=0;
    }
    void record(Snapshot *s)
    {
        if(head==nullptr){
            head=new TimelineNode;
            head->data=s;
            head->prev=nullptr;
            head->next=nullptr;
            tail=head;
            stepCount++;
             
        }
        else if(stepCount==1){
            TimelineNode* temp=new TimelineNode;
            temp->data=s;
            head->next=temp;
            tail->next=temp;
            temp->prev=head;
            temp->next=nullptr;
            tail=temp;
            stepCount++;
        }
        else{
            TimelineNode* temp=new TimelineNode;
            temp->data=s;
            temp->next=nullptr;
            temp->prev=tail;
            tail->next=temp;
            tail=tail->next;
            stepCount++;
        }
    }
    TimelineNode *begin()
    {
        TimelineNode* temp=nullptr;
        if(head==nullptr){
            cout<<"TIMELINE IS ALREADY EMPTHY THERE IS NO CODE EXECUTED "<<endl;
            return temp;
        }
        else{
            temp=head;
            return temp;
        }
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

// Core structs
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}

// resolve.bin - bookkeeping
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};



// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream &in, string &out)
{
    string wholeline;
    while(!in.eof()){
        getline(in,wholeline);
        if(in.fail()){
            break;
        }
        if(wholeline.size()>0 && wholeline[wholeline.size()-1]=='\r'){
            wholeline.pop_back();
        }
        int32_t check=0;
        for(int32_t i=0;i<wholeline.size();i++){
            if(wholeline[i]!=' ' && wholeline[i]!='\t'){
                check=1;
            }
        }
        if(check==1){
            out=wholeline;
            return true;
        }
    }
    return false;
}
string firstWord(const string &line)
{
    string word;
    int32_t count=0;
    for(int32_t i=0;i<line.size()&& (line[i]==' '||line[i]=='\t');i++){
        count++;
    }
    for(int i=count;i<line.size() && (line[i]!=' '&& line[i]!='\t');i++){
        word+=line[i];
    }
    return word;
}
string secondWord(const string &line)
{
    string word;
    int32_t count=0;
    for(int32_t i=0;i<line.size()&& (line[i]==' '||line[i]=='\t');i++){
        count++;
    }
    int32_t count1=count;
    for(int32_t i=count1;i<line.size() && (line[i]!=' '&& line[i]!='\t');i++){
        count++;
    }
    count1=count;
    for(int32_t i=count1;i<line.size()&& (line[i]==' '||line[i]=='\t');i++){
        count++;
    }
    for(int32_t i=count;i<line.size() && (line[i]!=' '&& line[i]!='\t');i++){
        word+=line[i];
    }
    return word;
}
bool validateProgram(const char *sourcePath)
{
    ifstream in(sourcePath);
    if(!in){
        cout<<"THERE IS SOME ERROR! FILE NOT OPEN"<<endl;
        return false;
    }
    int32_t check_func=0;
    string line;
    string word;
    while(readSourceLine(in,line)){
        word=firstWord(line);
        if(word=="func"){
            if(check_func==0){
              check_func=1;
            }
            else if(check_func==1){
                cout<<"ERROR: NESTED FUNCTION INCLUDE IN THIS PROGRAM " << endl ;
                return false;
            }
            
        }
        else if(word=="func_end"){
            if(check_func==1){
                check_func=0;
            }
            else if(check_func==0){
                cout<<"ERROR: FUNC_END IS WRITTEN INVALID " << endl;
                return false;
            }
        }
    }
    if(check_func==1){
        cout<<"FUNCTION HAS NOT ENDING FUNCTION PARAMETERS"<<endl;
        return false;
    }
    return true;
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f, int64_t offsetField, const string &text)
{
    int64_t size_ofsett=ftell(f);
    fwrite(&offsetField,sizeof(offsetField),1,f);
    int32_t size_of_text=text.length();
    fwrite(&size_of_text,sizeof(size_of_text),1,f);
   fwrite(text.c_str(), 1, text.size(), f);
    return size_ofsett;

}
int64_t readResolveRecord(FILE *f, string &outText)
{
    int64_t size_ofset=0;
    int64_t ans=-10;
    if(fread(&size_ofset,sizeof(size_ofset),1,f)!=1){
        return ans;
    }
    int32_t size_oftext=0;
    if(fread(&size_oftext,sizeof(size_oftext),1,f)!=1){
        return ans;
    }
    outText.clear();
    for (int32_t i = 0; i < size_oftext; ++i) {
        char c;
        if (fread(&c, sizeof(char), 1, f) != 1) {
            return ans;
        }
        outText.push_back(c); 
    }

    return size_ofset;
}
int64_t resolveProgram(const char *sourcePath, const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;
    FILE* f;
    ifstream fin;
    fin.open(sourcePath);
    if(!fin){
        cout<<"ERROR:FILE NOT OPENED"<<endl;
        return -1;
    }
    FILE* fout=fopen(resolveBinPath,"w+b");
    if(fout==nullptr){
        cout<<"ERROR: FILE NOT OPENED"<<endl;
        return -1;
    }
    string line;
    while(readSourceLine(fin,line)){
        string first=firstWord(line);
       
        if (first=="call"){
            string second=secondWord(line);
            if(patchCount<MAX_PATCHES){
                int64_t pos=ftell(fout);
                writeResolveRecord(fout,pos,line);
                patches[patchCount].targetFuncName=second;
                patches[patchCount].byteOffsetOfOffsetField=pos;
                patchCount++;         
            }
            else{
                fclose(fout);
                return -1;
            }
        }
        else if(first=="func"){
            string second=secondWord(line);
            if(funcCount<MAX_FUNCS){
                for(int i=0;i<funcCount;i++){
                    if(funcArray[i].funcName==second){
                        cout<<"DUPLICATE FUNCTIONS ERROR"<<endl;
                        fclose(fout);
                        return -1;
                    }
                }
                int64_t pos=ftell(fout);
                writeResolveRecord(fout,pos,line);
                funcArray[funcCount].funcName=second;
                funcArray[funcCount].byteOffsetInResolveBin=pos;
                funcCount++;
            }
            else{
                fclose(fout);
                return -1;
            }
        }
        else{
            int64_t pos=ftell(fout);
            writeResolveRecord(fout,pos,line);
        }
    }
    fin.close();
    int64_t ch=-1;
    for(int i=0;i<funcCount;i++){
        if(funcArray[i].funcName=="main"){
            ch=funcArray[i].byteOffsetInResolveBin;
        }
    }
    if(ch==-1){
        fclose(fout);
        return -1;
    }
    int64_t checkpatch=-2;
    int a=0;
    for(int i=0;i<patchCount;i++){
        a=0;
        for(int j=0;j<funcCount;j++){
            if(funcArray[j].funcName==patches[i].targetFuncName){
                checkpatch=funcArray[j].byteOffsetInResolveBin;
                a=1;
            }
        }
        if(a!=0){
            fseek(fout, patches[i].byteOffsetOfOffsetField, SEEK_SET);   
            fwrite(&checkpatch, sizeof(int64_t), 1, fout);       
        }
        else{
            fclose(fout);
            return -1;
        }
    }
    fclose(fout);
    return ch;
}

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
}
Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    // build the snapshot based on the callStack given
}
void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack

    // implementation:
    // execute line by line, and according to the keyword perform action
}

// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
// main section
int32_t main()
{
    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
if (mainOffset < 0)
{
    cout<<"THERE IS AN ERROR IN YOUR FILE CHECK AGAIN"<<endl;
    return 1;
}
else{
    cout<<"EVERYTHING IS CORRECTLY CREATED"<<endl;
}

    // if (!validateProgram("source.bin"))
    // {
    //     // send an error response instead of a .tdbg file
    //     return 1;
    // }

    // int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");

    // Timeline timeline;
    // executeProgram("resolve.bin", mainOffset, timeline);

    // writeTdbg(timeline, "session.tdbg");

    // return 0;
}