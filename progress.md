
## Day 01: October-02-2026

# What I have implemented
# STAGE-0 $
First I understood the whole project in detail, that is, what I should do.
I tried to download Linux on my laptop from September-29-2026, but there were a lot of errors when I was trying to download it. Then I watched YouTube videos and searched for the reasons for the errors on Google.
When I enabled virtualization through the BIOS, Ubuntu installed on October-01-2026.
Then I downloaded the compiler g++ version 15.2.0.
Then I downloaded the WSL extension in VS Code and connected VS Code with Linux through WSL.
Then I completed Stage 0 by creating the source.bin file in VS Code.
Then I wrote the demo code of C-- as given in the document into the source.bin file.

# From where I will start tomorrow
 First I will complete the stack timeline classes which is important for data storing.
 Then i will complete Stage 1.

 ## DAY 02: October-04-2026

 # What I have implemented
 # Stage 1 $
 First I complete the stack class through linked-list which is important for storing every function call in c-- every liner is stored inside stack.
 Then I complete the Timeline class which store all the snapshots in c-- program execution file.It just store the every line dont remove every line. In this we use doublelinkedlist for storing executions lines.
 Then i understand the structs and class you gave us in the file,that why it used in this program.
 Then I write the code of readsourcefile function which read the whole execution file from c-- program execution file.
 Then I completed the firstword function code which save the first word of every line of the execution file so that we can check which instruction keyword used in c-- language.
 Then i completed the secondword function code which saves the second word of every line of the execution file so that we can check this is which identifier.
 Then i completed the function of getvalidated function which check thats whether the function call is completed or not means if function is defined then can a function end or not.

 # From where I will start tomorrow
 First I will complete the Stage 2 Resolve.

  ## DAY 02: October-09-2026
# What I have implemented
 # Stage 2 $
 First i understand stage 2 that what should i do in this stage it takes more than 1 day of me then i start code this . First i start the code of resolveprogram then in the mid when i write my line in resolve.bin thropugh binary i complete the function of writeresolve then write every function with their bytes in resolve.bin and i also check if there is some function then add it to funcArray and if their is some call then add it to patches afre that i can check is there any main in your code or not through funcarray struct i check this array of struct one by one if there is main then store it for return otherwise return error then i check the call function that in call function is function really exist by checking with funcArray if there is any patch dont exist in funcArray then return error.If i find the call function then i write the bytes of function into call function by fseek function.This is all which i have my implemented in my stage 2.

 # From where I will start tomorrow
 First I will complete the Stage 3.

  ## DAY 03: October-12-2026
# What I have implemented
 # Stage 3 $
 First i understand stage 3 after that i first created helper functions in page that which should i create, then i simply write helper ehich one is getting index of variable from frame or locals then getting value of that variable from frame assigning value to the variable and final helper function is execute compiler by operators like arithmetic operators. then i move to the main function which is execute function first i open the file then i created call stack which store every line of function execution then i start execution through main file then i call these functions to read and tokenize lines. Then i execute instructions through helper functions then check call function. Then check function returns then i store every record in snapshot. Then i check the exit function thats all in stage 3.

 # From where I will start tomorrow
 First I will complete the Stage 4.

