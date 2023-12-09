Hi, this is Project4 for IERG4180.\
Name: Hangyu CHEN\
SID: 1155197448


## 1. Video recorded that shows the final result of my code (Plz change to 4k when watching it!):
[![Socket Programming wiz threadpool](https://img.youtube.com/vi/sTgdCjFESE8/0.jpg)](https://www.youtube.com/watch?v=sTgdCjFESE8)

In the left Ubuntu, 
* the server is at the top left
* an client which receive tcp is at the top right
* an client which send tcp is at the bottom left
* an client which send request is at the bottom right

In the right Windows,
* an client which receive udp is at the top
* an client which send udp is at the bottom

Link: https://www.youtube.com/watch?v=sTgdCjFESE8

## 2. Command for compiling and executing the code
As I have already compiled the code as two files: server and client. So you can simply using command such as:
```
./server
```
and
```
./client -send -proto tcp
```
to excute the code

Of course, you can complie the code using:
```
g++ threadpool.cpp main.cpp -o server -pthread
```
and
```
g++ client.cpp -o client
```

**Please be aware that I used ms for the -stat command. In this case, -stat 1000 means the Elapsed will be 1 second (1000 ms).**

### 2.1. Please describe how the server makes sure to close all the sockets and indicate the specific line(s) of code responsible for this. (Very important)
As for the tcp transmission, we only used the socket which is originally used for sending the arguments from client. \
In this case, we close this socket when the client (user) using Ctrl+C or sending all the required pktnum.

As for the udp transmission, we used two sockets, one is originally used for sending the arguments from client (tcp socket), the other is used for transmitting data (udp socket).\

When the client sending udp/ Server receiving udp:\
For Client:
* the udp socket in the client will be closed after using Ctrl+C or sending all the required pktnum
* then client send a "finish" message to the server using tcp socket
* after sending that "finish" message, client close the original tcp socket.
This code could be found from line 758-763 in client.cpp.
For Server:
* the server will keep listen the tcp socket, and if it receives the "finish" message, it will break the loop for receiving data
* then, server close both udp and tcp socket
This code could be found from line 626-628 in main.cpp


When the client receiving udp/ Server sending udp:\
For Client:
* It will keep sending "notfinish" message to the Server after receiving udp data. (line 906 in client.cpp)
* It will stop sending the "notfinish" message when using Ctrl+C or receving all the required pktnum (recv == 0)
* Then, it close the tcp socket and the udp socket. (line 922, 935)
This code could be found from line 908 and line 935 in client.cpp

For Server:
* It will keep receiving message from Client to receive the "notfinish" message
* If it successfully receive that message, it means Client is still open and Server could send TCP data. (line 312 in main.cpp)
* If it receive nothing (read == 0), it will stop sending data and break the loop to close the tcp socket. (line 316 in main.cpp)


### 2.2. Please describe how you control the pool size and indicate the specific line(s) of code responsible for this.  (Very important)
I set up an indepentent thread in the threadpool which is only used for adjusting the size and print out the size of the current threadpool with a given time (Elapsed).\
It could be found from the function
```
void *adjust_thread(void *threadpool)
```
in the threadpool.cpp (at line 293)

### 2.3. Do you disable Nagle algorithm? And indicate the specific line(s) of code responsible for this.
Yes, I used the code in the tutorial to disable it in the response mode.\
```
int one = 1;
setsockopt(sock, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
```
It could be found at line 940 in client.cpp and line 1039 in main.cpp
