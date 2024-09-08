#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <sstream>
#include <thread>
#include <fstream>

// Function to split the message based on delimiter(usually CRLF) -> returns the vector containing [request_line, headers, request_body]
std::vector<std::string> split_message(const std::string &message, const std::string& delim) {
  std::vector<std::string> toks;
  std::stringstream ss = std::stringstream{message};
  std::string line;
  while (getline(ss, line, *delim.begin())) {
    toks.push_back(line);
    ss.ignore(delim.length() - 1);
  }
  return toks;
}

// Function to get the requested path in the http request
std::string get_path(std::string request) {
  std::vector<std::string> toks = split_message(request, "\r\n");
  std::vector<std::string> path_toks = split_message(toks[0], " ");
  return path_toks[1];
}

// Function to get the client request header
std::vector<std::string> get_header(std::string request)
{
  std::vector<std::string> toks = split_message(request, "\r\n");
  std::vector<std::string> ans;
  for(int i = 1; i < toks.size()-2; i++)
  {
    ans.push_back(toks[i]);
  }
  return ans;
}

std::string get_body(std::string request)
{
  std::vector<std::string> toks = split_message(request, "\r\n");
  return toks[toks.size()-1];
}

// Here we assume that the file request will only comprise of GET and POST requests.
std::string handle_file_request(bool is_get_request, std::string dir, std::string filename, std::string body = "")
{
  std::string message;
  if(is_get_request)
  {
    std::ifstream ifs(dir+filename);

    if(ifs.good())
    {
      std::stringstream content;
      content << ifs.rdbuf();
      message = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: "+std::to_string(content.str().length())+"\r\n\r\n"+content.str()+"\r\n";
    } 
    else
    {
      message = "HTTP/1.1 404 Not Found\r\n\r\n";
    }
  }
  else 
  {
    std::ofstream ofs(dir+filename);
    ofs << body;
    message = "HTTP/1.1 201 Created\r\n\r\n";
    ofs.close();
  }

  return message;
}

int handle_http(int client_fd, struct sockaddr_in client_addr, std::string dir)
{

// Buffer to read the message / request from the client
  std::string client_message(1024, '\0');

// Reading the message/request from the client
  ssize_t brecvd = recv(client_fd, (void*) &client_message[0], client_message.max_size(), 0);
  if(brecvd < 0)
  {
    std::cerr << "error receiving message from client\n";
    close(client_fd);
    return 1;
  }

// Logging the received client request/message
  std::cerr<<"Client Message (length: "<<client_message.size()<<")"<<std::endl;
  std::clog<<client_message<<std::endl;

  // std::string message = client_message.starts_with("GET / HTTP/1.1\r\n") ? "HTTP/1.1 200 OK\r\n\r\n" : "HTTP/1.1 404 Not Found\r\n\r\n";
  // std::string str = client_message.substr(10, client_message.find(' ', 10) - 10);
  // std::string message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+std::to_string(str.length())+"\r\n\r\n"+str;

// Fetch the requested path in the client request
  std::string path = get_path(client_message);

// Split the requested paths into discrete levels
  std::vector<std::string> split_paths = split_message(path, "/");

// Create the server response to client(message) as per the requested path
  std::string message;
  if(path == "/")
  {
    message = "HTTP/1.1 200 OK\r\n\r\n";
  }
  else if(split_paths[1]=="echo")
  {
    message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+std::to_string(split_paths[2].length())+"\r\n\r\n"+split_paths[2];
  }
  else if(split_paths[1]=="user-agent")
  {
    std::vector<std::string> headers = get_header(client_message);
    std::string user_agent;
    for(int i = 0; i < headers.size(); i++)
    {
      if(headers[i].starts_with("User-Agent"))
      {
        user_agent = split_message(headers[i], " ")[1];
      }
    }
    message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+std::to_string(user_agent.length())+"\r\n\r\n"+user_agent;
  }
  else if(split_paths[1]=="files")
  {
    std::string filename = split_paths[2];

    message = handle_file_request(client_message.starts_with("GET"), dir, filename, get_body(client_message));
  }
  else
  {
    message = "HTTP/1.1 404 Not Found\r\n\r\n";
  }

  std::cout<<"Response: "<<message<<std::endl;
  ssize_t bsent = send(client_fd, message.c_str(), message.size(), 0);

  return 0;
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::string dir;
  if(argc == 3 && strcmp(argv[1], "--directory") == 0)
  {
    dir = argv[2];
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  std::cout << "Waiting for a client to connect...\n";

  // int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
  // std::string message = "HTTP/1.1 200 OK\r\n\r\n";
  // send(client, message.c_str(), message.length(), 0);
  // std::cout << "Client connected\n";

  int client_fd;
  while(true)
  {
    client_fd = accept(server_fd, (struct sockaddr*)& client_addr, (socklen_t*)&client_addr_len);
    std::cout<< "Client connected\n";
    // std::thread th(handle_http, client_fd, client_addr, dir);
    std::thread th(handle_http, client_fd, client_addr, dir);
    th.detach();
  }

  close(server_fd);

  return 0;
}
