Hi, this is Project4 for IERG4180.\
Name: Hangyu CHEN\
SID: 1155197448

## 1. Command for compiling and executing the code
As I have already compiled the code as two files: server and client. So you can simply using command such as:
```
./server
```
and
```
./client -send -proto tcp
```
```
./client -http -url http://localhost:8080/index.html
```
```
./client -http -url https://localhost/index.html
```
```
./client -http -url https://www.ietf.org/rfc/rfc2616.txt -file text.txt
```
to excute the code

Of course, you can complie the code using:
```
g++ threadpool.cpp main.cpp -o server -pthread -lssl -lcrypto
```
and
```
g++ client.cpp -o client -pthread -lssl -lcrypto
```

**Please be aware that I used ms for the -stat command. In this case, -stat 1000 means the Elapsed will be 1 second (1000 ms).**

## 2. Using Openssl to make a self-signed cert for authenticating itself
It includes:
(a) secure communications over SSL/TLS;\ 
(b) authentication of server’s digital certificates;\ 
(c) support Server Name Indication (SNI);\ 
and (d) performs server hostname verification.\

### 2.1 Server Certificate (`domain.crt`) and Private Key (`domain.key`):
- You can either generate a self-signed certificate or obtain a certificate from a trusted certificate authority (CA).
- If you want to generate a self-signed certificate, you can use OpenSSL to generate a private key and a self-signed certificate:
```
openssl req -newkey rsa:2048 -nodes -keyout domain.key -x509 -days 365 -out domain.crt
```
- This command will generate a 2048-bit RSA private key (`domain.key`) and a self-signed certificate (`domain.crt`) valid for 365 days. You will be prompted to enter some information for the certificate during the process.
- If you have obtained a certificate from a CA, make sure you have both the certificate file and the corresponding private key file.

### 2.2 Client Certificate Authority (CA) Certificate (`rootCA.crt`):
- If you are using self-signed certificates for client authentication, you will need to generate a CA certificate (self-signed) and use it to sign the client's certificate.
- To generate a CA certificate, you can use a similar OpenSSL command as before:
```
openssl req -newkey rsa:2048 -nodes -keyout rootCA.key -x509 -days 365 -out rootCA.crt
```
- This command will generate a CA private key (`rootCA.key`) and a self-signed CA certificate (`rootCA.crt`) valid for 365 days.
- Make sure to keep the private key (`rootCA.key`) secure and use it to sign the client certificates.
