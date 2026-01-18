#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char *argv[])
{
   int server_fd, client_fd;
   struct sockaddr_in6 address;
   int opt = 1;
   int addrlen = sizeof(address);
   int port = 1848;

   if (argc > 1)
      port = atoi(argv[1]);

   if ((server_fd = socket(AF_INET6, SOCK_STREAM, 0)) == 0)
   {
      perror("socket failed");
      exit(EXIT_FAILURE);
   }

   if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
   {
      perror("setsockopt");
      exit(EXIT_FAILURE);
   }

   // Allow IPv4 connections on IPv6 socket
   int no = 0;
   if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)))
   {
      perror("setsockopt IPV6_V6ONLY");
      exit(EXIT_FAILURE);
   }

   address.sin6_family = AF_INET6;
   address.sin6_addr = in6addr_any;
   address.sin6_port = htons(port);

   if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
   {
      perror("bind failed");
      exit(EXIT_FAILURE);
   }

   if (listen(server_fd, 3) < 0)
   {
      perror("listen");
      exit(EXIT_FAILURE);
   }

   printf("Server listening on port %d\n", port);

   while (1)
   {
      if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0)
      {
         perror("accept");
         continue;
      }

      printf("Connection accepted\n");
      write(client_fd, "hello\n", 6);
      printf("Data written\n");
      close(client_fd);
      printf("Connection closed\n");
   }

   close(server_fd);
   return 0;
}
