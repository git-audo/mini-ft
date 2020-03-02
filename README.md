# Mini-ft
C implementation of a distributed file transfer application, with a cli client.
The server can handle multiple concurrent connections, delegating each request to a new process.

Possible improvements:
- restarting file tranfer from where it was left in case of connetion errors
- secure/encrypted file transfers
- user authentication
- integration of a log system