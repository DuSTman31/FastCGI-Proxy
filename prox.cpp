#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>


#include <map>
using std::map;

struct sockPair{
  int s1;
  int s2;
};

map<int, sockPair*> sockPairs;


bool setNonBlock(int fh)
{
  int flags = fcntl(fh, F_GETFL, 0);
  if (flags < 0) return false;
  flags = flags|O_NONBLOCK;
  return (fcntl(fh, F_SETFL, flags) == 0) ? true : false;
}


int connected(int epollSock)
{
  int status = 0;
  struct epoll_event ne;
  struct epoll_event events[10];

  while(true)
    {
      int noRdy = epoll_wait(epollSock, events, 10, 100);
      for(unsigned int i = 0 ; i!=noRdy ; i++)
	{
	  if(events[i].data.fd == STDIN_FILENO)
	    {
	      if(events[i].events | EPOLLIN)
		{
		  sockaddr_in addr;
		  int inSock = accept(STDIN_FILENO, NULL, NULL);
		  int outSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		  if(outSock == -1)
		    {
		      printf("Error creating socket.\n");
		      return 1;
		    }
		  
		  addr.sin_family = AF_INET;
		  addr.sin_port = htons(9001);
		  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		  
		  
		  status = connect(outSock, (struct sockaddr*)&addr, sizeof(addr));
		  if(status == -1)
		    {
		      printf("Could not connect.\n");
		      return 2;
		    }

		  setNonBlock(inSock);
		  setNonBlock(outSock);
		  
		  sockPair *socks = new sockPair;
		  socks->s1 = inSock;
		  socks->s2 = outSock;
		  
		  sockPairs[inSock] = socks;
		  sockPairs[outSock] = socks;

		  ne.data.fd = inSock;
		  ne.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP;
		  
		  epoll_ctl(epollSock, EPOLL_CTL_ADD, inSock, &ne);
		  
		  ne.data.fd = outSock;
		  epoll_ctl(epollSock, EPOLL_CTL_ADD, outSock, &ne);

		}
	      else if(events[i].events | EPOLLRDHUP)
		{
		  return 3;
		}
	    }
	  else
	    {
	      if(events[i].events | EPOLLIN)
		{
		  sockPair *ap = sockPairs[events[i].data.fd];

		  if(ap->s1 == events[i].data.fd)
		    {
		      int bytesRead = 0;
		      char *buf = new char[1024];
		      bytesRead = read(ap->s1, buf, 1024);
		      if(bytesRead == -1)
			{
			  free(buf);
			}
		      send(ap->s2, buf, bytesRead, 0);

		      delete[] buf;
		    }
		  else
		    {
		      int bytesRead = 0;
		      char *buf = new char[1024];
		      bytesRead = read(ap->s2, buf, 1024);
		      if(bytesRead == -1)
			{
			  free(buf);
			}
		      send(ap->s1, buf, bytesRead, 0);
		      
		      delete[] buf;
		    }

		}
	      else if((events[i].events | EPOLLRDHUP) || (events[i].events | EPOLLHUP))
		{
		  sockPair *ap = sockPairs[events[i].data.fd];

		  epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s1, NULL);
		  epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s1, NULL);

		  sockPairs.erase(ap->s1);
		  sockPairs.erase(ap->s2);

		  close(ap->s1);
		  close(ap->s2);

		  delete ap;
		}

	    }
	}
    }
}

int main()
{
  struct sockaddr_in addr;
  int status = 0;

  int pollSocket = epoll_create1(0);
  int ls = listen(STDIN_FILENO, 10);
  epoll_event ne;

  ne.events = EPOLLIN | EPOLLRDHUP;
  ne.data.fd = STDIN_FILENO;
  epoll_ctl(pollSocket, EPOLL_CTL_ADD, STDIN_FILENO, &ne);


  return connected(pollSocket);
}

