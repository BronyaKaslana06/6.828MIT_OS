#include "kernel/types.h"
#include "user/user.h"

#define READ 0
#define WRITE 1

int main(int argc, char *argv[]){
	char ch = 'b';	//通过pingpong传递的字节
	int sonToParent[2];
	pipe(sonToParent);
	int parentToSon[2];
	pipe(parentToSon);

	int pid = fork();
	int errStatus = 0;
	if(pid<0){
		printf("fork创建子进程出错\n");
		close(sonToParent[READ]);
		close(sonToParent[WRITE]);
		close(parentToSon[READ]);
		close(parentToSon[WRITE]);
		exit(1);
	}
	else if(pid==0){	//子进程创建成功
		close(parentToSon[WRITE]);
		close(sonToParent[READ]);
		if(read(parentToSon[READ], &ch, sizeof(char)) != sizeof(char)){
			printf("子进程read出错\n");
			errStatus = 1;
		}
		else{
			printf("%d: received ping\n", getpid());
		}
		if(write(sonToParent[WRITE], &ch, sizeof(char)) != sizeof(char)){
			printf("子进程write出错\n");
			errStatus = 1;
		}
		close(parentToSon[READ]);
		close(sonToParent[WRITE]);
		exit(errStatus);
	}
	else{//父进程
		close(parentToSon[READ]);
		close(sonToParent[WRITE]);
		if(write(parentToSon[WRITE], &ch, sizeof(char))!=sizeof(char)) {
			printf("父进程write出错");
			errStatus = 1;
		}
		if(read(sonToParent[READ],&ch, sizeof(char))!=sizeof(char)) {
			printf("父进程read出错");
			errStatus = 1;
		}
		else{
			printf("%d: received pong\n", getpid());
		}
		close(parentToSon[WRITE]);
		close(sonToParent[READ]);
		exit(errStatus);
	}
}




