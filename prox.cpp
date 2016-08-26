#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
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
  char *inBuf;
  char *outBuf;
};

map<int, sockPair*> sockPairs;

const unsigned int bufSize = 10240;


// Addresses for connection.
const bool unsock = false; // Set true to connect to daemon via unix domain socket. Else uses ip.
const char *unix_path = "/var/run/hhvm";  // Endpoint address for unix socket connection
const int inport = 9001;
const int32_t inaddr = INADDR_LOOPBACK;


bool setNonBlock(int fh)
{
  int flags = fcntl(fh, F_GETFL, 0);
  if (flags < 0) return false;
  flags = flags|O_NONBLOCK;
  return (fcntl(fh, F_SETFL, flags) == 0) ? true : false;
}


int connect_un()
{
  int status = 0;
  sockaddr_un addr;
  int outSock = socket(AF_UNIX, SOCK_STREAM, 0);

  if(outSock == -1)
    {
      printf("Error creating socket.\n");
      return -1;
    }

  if(strlen(unix_path) > 108)
    {
      printf("Path length exceeds max unix socket length");
      return -1;
    }
		  
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, unix_path);
		  
  status = connect(outSock, (struct sockaddr*)&addr, sizeof(addr));
  if(status == -1)
    {
      printf("Could not connect.\n");
      return -2;
    }

  setNonBlock(outSock);
  return outSock;
}

int connect_in()
{
  int status = 0;
  sockaddr_in addr;
  int outSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if(outSock == -1)
    {
      printf("Error creating socket.\n");
      return -1;
    }
		  
  addr.sin_family = AF_INET;
  addr.sin_port = htons(inport);
  addr.sin_addr.s_addr = htonl(inaddr);
		  
		  
  status = connect(outSock, (struct sockaddr*)&addr, sizeof(addr));
  if(status == -1)
    {
      printf("Could not connect.\n");
      return -2;
    }

  setNonBlock(outSock);
  return outSock;
}

int connect()
{
  if(unsock)
    {
      return connect_un();
    }
  else
    {
      return connect_in();
    }
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
		  int inSock = accept(STDIN_FILENO, NULL, NULL);
		  int outSock = connect();

		  if(outSock < 0)
		    {
		      return -1;
		    }

		  setNonBlock(inSock);
		  
		  sockPair *socks = new sockPair;
		  socks->s1 = inSock;
		  socks->s2 = outSock;
		  socks->inBuf = new char[bufSize];
		  socks->outBuf = new char[bufSize];
	  
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
		      char *buf = ap->inBuf;
		      bytesRead = read(ap->s1, buf, bufSize);
		      if(bytesRead > 0)
			{
			  send(ap->s2, buf, bytesRead, 0);
			}
		      else
                        {
                          if(bytesRead == 0)
			    {
			      sockPair *ap = sockPairs[events[i].data.fd];

			      epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s1, NULL);
			      epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s2, NULL);

			      sockPairs.erase(ap->s1);
			      sockPairs.erase(ap->s2);

			      close(ap->s1);
			      close(ap->s2);

			      delete[] ap->inBuf;
			      delete[] ap->outBuf;

			      delete ap;

			    }
                        }
		    }
		  else
		    {
		      int bytesRead = 0;
		      char *buf = new char[bufSize];
		      bytesRead = read(ap->s2, buf, bufSize);
		      if(bytesRead > 0)
			{
			  send(ap->s1, buf, bytesRead, 0);
			}
                      else
                        {
			  if(bytesRead == 0)
			    {
			      sockPair *ap = sockPairs[events[i].data.fd];
			      
			      epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s1, NULL);
			      epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s2, NULL);
			      
			      sockPairs.erase(ap->s1);
			      sockPairs.erase(ap->s2);
			      
			      close(ap->s1);
			      close(ap->s2);
			      
			      delete[] ap->inBuf;
			      delete[] ap->outBuf;
			      
			      delete ap;
			    }
                        }
	      

		    }

		}
	      else if((events[i].events | EPOLLRDHUP) || (events[i].events | EPOLLHUP))
		{
		  sockPair *ap = sockPairs[events[i].data.fd];

		  epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s1, NULL);
		  epoll_ctl(epollSock, EPOLL_CTL_DEL, ap->s2, NULL);

		  sockPairs.erase(ap->s1);
		  sockPairs.erase(ap->s2);

		  close(ap->s1);
		  close(ap->s2);

		  delete[] ap->inBuf;
		  delete[] ap->outBuf;

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

