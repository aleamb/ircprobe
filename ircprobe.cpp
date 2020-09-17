/*

	 Alejandro Ambroa 
	 Septiembre 2020

 */

#include <netdb.h>
#include <signal.h>
#include <arpa/inet.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <map>
#include <sstream>
#include <string>
#include <iostream>
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <algorithm>
#include <stdarg.h>
#include <list>
#include <getopt.h>

//#include <netinet/in.h>

#define CLOCK_INTERVAL 20

#define DEFAULT_PORT "6667"
#define DEFAULT_CONNECTION_INTERVAL 300
#define DEFAULT_NUM_CHANNELS 5
#define DEFAULT_MIN 1000
#define DEFAULT_MAX 20000
#define DEFAULT_TEST_INTERVAL 100
#define DEFAULT_STATS_INTERVAL 10000


#define MAX_EVENTS 10


const char* usage = "[-bd] [-h host/ip] [-n connections] [-p port] [-c channel]\n" \
										"                [--min=minimum] [--max=maximum] [--connin=interval]\n" \
										"                [--chnum=channels] [--cprefix=prefix]\n" \
										"                [--nprefix=prefix] [--help]\n";

const char* summary =  " -b                 Disable tests.\n" \
											 " -d                 Enable debug. Writes all irc server ouput in a file.\n" \
											 "                    with name <host>.log.\n" \
											 " -h host            Set irc host to connect.\n" \
											 " -n length          Set how many connections.\n" \
											 " -p port            IRC port (default 6667)\n" \
											 " -c channel         Connect all sockets only to this channel.\n" \
											 " --chnum=channels   Number of channels to distribute connections (default 5)\n" \
											 " --connin=interval  Interval (in ms) between sequential connections (default 300)\n" \
											 " --min=milis        Minimal range of time (in ms) in which each connection will\n" \
											 "                    send a message (default 1000 ms)\n" \
											 " --max=milis       Maximum range of time in which each connection will\n" \
											 "                    send a message (default 10000 ms)\n" \
											 " --cprefix=prefix  Nick prefix for connections (default 'test')\n" \
											 " --nprefix=prefix  Channel prefix for connections (default 'test')\n" \
											 " --help            This message.\n";


struct cstatus
{
				int socket;
				int status;
				std::string nick;
				std::string channel;
				long int planned;
};

struct mtest
{
				int msgs;
				int ms;
};

struct log_context {
				bool enable;
				int level;
				FILE *logfile;
};

struct log_context LOG;


int plan_min = DEFAULT_MIN;
int plan_max = DEFAULT_MAX;


void log(const char* format, ...) {
				if (LOG.enable) {

								va_list args;
								va_start (args, format);
								vfprintf (LOG.logfile, format, args);
								va_end (args);
				}
}

long int current_timestamp() {
				struct timeval te; 
				gettimeofday(&te, NULL); // get current time
				long int milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
				return milliseconds;
}


void splan(std::list<struct cstatus*>& planning,struct cstatus* cs) {

				cs->planned = (current_timestamp() + plan_min) + (rand() % (plan_max - plan_min));

				if (planning.empty()) {
								planning.push_back(cs);
				} else {

								std::list<struct cstatus*>::iterator it;

								for (it = planning.begin(); it != planning.end(); it++) {
												struct cstatus* tcs = *it;
												if (tcs->planned >= cs->planned) {
																break;
												}
								}

								planning.insert(it, cs);

				}
}

struct cstatus* splan_get(std::list<struct cstatus*>& planning) {

				time_t p = current_timestamp();
				struct cstatus* cs;

				if (planning.empty()) return NULL;

				std::list<struct cstatus*>::iterator it = planning.begin();

				if ((*it)->planned > p) return NULL;

				cs = *it;

				planning.erase(it);

				return cs;

}



void print_usage() {

				std::cout << "usage: ircprobe " << usage;

}

void print_help() {
				print_usage();
				std::cout << "Command Summary:" << std::endl << summary << std::endl; 
}

int main(int argc, char **argv)
{

				struct addrinfo hints, *hostinfo;
				int rv = -1;
				int epoll_fd;

				std::map<int, struct cstatus*> sockets;
				std::vector<int> fds;
				std::list<struct cstatus*> planning;

				struct mtest current_test;

				int nfds;

				int msgs = 0;

				int resultsw = 0;
				int sockets_connected = 0;

				struct epoll_event events[MAX_EVENTS];

				std::stringstream sbuffer;
				std::string server;
				std::string raw;
				std::string nick;
				std::string msg;

				char buffer[512];
				char line[512];
				char file[32];

				bool execute_test = true;
				bool sockets_finished = false;
				bool execute_behavior = true;


				int max_channels = DEFAULT_NUM_CHANNELS;
				int notice_interval = DEFAULT_STATS_INTERVAL;
				int test_interval = DEFAULT_TEST_INTERVAL;
				int sockets_interval = DEFAULT_CONNECTION_INTERVAL;


				int tin = 0;
				int sin = 0;
				int nin = 0;
				int bin = 0;
				int si = 0;
				FILE *data;
				int running = 1;


				bool one_channel = false;
				std::string host;
				std::string port = DEFAULT_PORT;
				std::string channel;
				std::string nprefix = "test";
				std::string cprefix = "test";

				int num_connections = 0;

				long int t_start = 0;


				static struct option long_options[] = {
								{ "host",      required_argument,       0,  'h' },
								{ "conn",      required_argument,       0,  'n' },
								{ "port",      required_argument,       0,  'p' },
								{ "channel",   required_argument,       0,  'c' },
								{ "connin",    required_argument,       0,   0  },  
								{ "chnum",     required_argument,       0,   0  },
								{ "min",       required_argument,       0,   0  },
								{ "max",       required_argument,       0,   0  },
								{ "nprefix",   required_argument,       0,   0  },
								{ "cprefix",   required_argument,       0,   0  },
								{ "help",      no_argument,             0,   0  },
								{  0,             0,                    0,   0  }
				};

				/*

					 if (argc < 2) {
					 print_usage(); 
					 exit(EXIT_FAILURE);
					 }
				 */

				int long_index =0;
				int opt;
				while ((opt = getopt_long(argc, argv,"bdh:n:p:c:",  long_options, &long_index )) != -1) {

								if (long_index > 3) {
												switch (long_index) {
																case 4:

																				sockets_interval = atoi(optarg);
																				if (sockets_interval <= 0) {
																								std::cout << "Intervalo de conexion debe ser mayor que 0" << std::endl;
																								exit(EXIT_FAILURE);
																				}
																				break;


																case 5:

																				max_channels = atoi(optarg);
																				if (max_channels <= 0) {
																								std::cout << "Numero de canales debe ser mayor que 0" << std::endl;
																								exit(EXIT_FAILURE);
																				}
																				break;

																case 6:
																				plan_min = atoi(optarg);
																				break;

																case 7:
																				plan_max = atoi(optarg);
																				break;


																case 8:
																				nprefix = optarg;


																case 9:
																				cprefix = optarg;
																				break;
																case 10:
																				print_help();
																				exit(0);

												}
								} else {


												switch (opt) {
																case 'h' : host = optarg;
																					 break;
																case 'n' : num_connections = atoi(optarg);
																					 break;
																case 'p' : port = optarg; 
																					 break;
																case 'c' :
																					 one_channel = true;
																					 channel = optarg;
																					 if (channel.at(0) != '#') { 
																									 channel.insert(channel.begin(), '#');
																					 }
																					 break;
																case 'b':
																					 execute_test = false;
																					 break;
																case 'd':
																					 LOG.enable = true;
																					 break;
																case '?': print_usage(); 
																					exit(EXIT_FAILURE);
												}


								}

				}

				if (host.empty() || num_connections == 0) {
								print_usage(); 
								exit(EXIT_FAILURE);
				}

				if (plan_max < plan_min) {
								std::cout << "El rango minimo de mensajes no puede ser mayor que el maximo." << std::endl;
								exit(EXIT_FAILURE);
				}

				if (plan_min <= 0 || plan_max <= 0) {
								std::cout << "los rangos deben ser mayores que cero" << std::endl;
								exit(EXIT_FAILURE);
				}


				std::cout << "El host es: " << host << std::endl;
				std::cout << "El puerto es: " << port << std::endl;
				std::cout << "El número de usuarios a simular es: " << num_connections << std::endl;
				std::cout << "Los usuarios se conectarán al IRC con un intervalo de: " << sockets_interval << " milisegundos." << std::endl;
				std::cout << "Los usuarios enviarán mensajes en un rango de: " << plan_min << " - " << plan_max << " milisegundos." << std::endl;
				std::cout << "Prefijo para nicks: " << nprefix << std::endl;
				std::cout << "Prefijo para canales: " << cprefix << std::endl;


				if (one_channel) {
								std::cout << "Se unirán todos a " << channel << std::endl;
				} else {
								std::cout << "Se distribuirán en " << max_channels <<" canales." << std::endl;
				}
				if (LOG.enable) {

								std::cout << "Log habilitado. " << std::endl;
				} else {

								std::cout << "Log DEShabilitado. "  << std::endl;
				}


				if (LOG.enable) {
								sprintf(file, "%s.log", host.c_str());
								LOG.logfile = fopen(file, "w");
				}

				current_test.ms = 0;
				current_test.msgs = 0;

				memset(&hints, 0, sizeof(hints));
				hints.ai_family = AF_INET;
				hints.ai_socktype = SOCK_STREAM;

				epoll_fd = epoll_create1(0);

				if (epoll_fd == -1)
				{
								perror("Error creating epoll");
								exit(1);
				}

				struct epoll_event ev1;
				ev1.events = EPOLLIN;
				ev1.data.fd = 0;

				if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, 0, &ev1) == -1)
				{
								perror("epoll_ctl: add stdin");
								close(epoll_fd);
								exit(EXIT_FAILURE);
				}

				sprintf(file, "%s.data", host.c_str());

				data = fopen(file, "w");


				if ((rv = getaddrinfo(host.c_str(), port.c_str(), &hints, &hostinfo)) != 0)
				{
								fprintf(stderr, "getaddrinfo %s\n", gai_strerror(rv));
								close(epoll_fd);
								exit(1);
				}




				std::cout << "Los resultados se escribirán en " << host << ".data" << std::endl;
				std::cout << "Ejecutando..." << std::endl;

				t_start = current_timestamp();
				while (running)
				{

								nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, CLOCK_INTERVAL);

								if (nfds == -1)
								{
												perror("epoll_pwait");
												exit(EXIT_FAILURE);
								}
								else if (nfds == 0)
								{	
												sin += CLOCK_INTERVAL;
												tin += CLOCK_INTERVAL;
												nin += CLOCK_INTERVAL;

												if (nin > notice_interval) {
																nin = 0;

																fprintf(data, "%d %d\n", (int)((current_timestamp() - t_start)/1000), current_test.ms);	
																fflush(data);

																//std::cout << "Lag medio: " << current_test.ms << std::endl; 
												}
												if (execute_test && tin > test_interval) {
																tin = 0;

																if (sockets.size() > 0)
																{

																				struct cstatus* cs = splan_get(planning);

																				if (cs && cs->status == 4) {

																								int mi = sprintf(buffer, "PRIVMSG %s :%ld\n", cs->channel.c_str(), current_timestamp());

																								write(cs->socket, buffer, mi);
																								log("Send test: %s", buffer);
																								msgs++;


																								mi = sprintf(buffer, "PING :%ld\n", current_timestamp());

																								write(cs->socket, buffer, mi);
																								log("Send test: %s", buffer);
																								msgs++;
																								splan(planning, cs);
																				}
																}
												}

												if (sin > sockets_interval) {

																sin = 0;

																if (!sockets_finished)
																{

																				int s = socket(hostinfo->ai_family, hostinfo->ai_socktype, hostinfo->ai_protocol);

																				if (s != -1)
																				{

																								if (connect(s, hostinfo->ai_addr, hostinfo->ai_addrlen) == -1)
																								{
																												perror("Error connecting socket");
																												close(s);
																								}
																								else
																								{

																												struct epoll_event ev2;

																												ev2.events = EPOLLIN;
																												ev2.data.fd = s;

																												if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, s, &ev2) == -1)
																												{
																																perror("Error polling socket");
																																close(s);
																												}
																												else
																												{
																																struct cstatus* cs = new struct cstatus;
																																char nick[16];
																																char bfc[16];

																																sprintf(nick, "%s%d", nprefix.c_str(), si);
																																sprintf(bfc,  one_channel ? channel.c_str()  : "%s%d", cprefix.c_str(), rand()%max_channels);


																																cs->socket = s;
																																cs->nick = nick;
																																cs->channel = bfc;
																																cs->status = 0;

																																splan(planning, cs);

																																std::string com;
																																com = "NICK " + cs->nick + "\n";
																																write(s, com.c_str(), com.size());
																																log("Send -> %s", com.c_str());
																																com = "USER " + cs->nick + " " + cs->nick + " " + cs->nick + " :" + cs->nick + "\n";
																																write(s, com.c_str(), com.size());
																																log( "Send -> %s", com.c_str());
																																cs->status = 2;


																																sockets.insert(std::pair<int, struct cstatus*>(s, cs));
																																fds.push_back(s);
																																sockets_connected++;



																												}
																								}
																				}
																				else
																				{
																								perror("Error Creating socket: ");
																				}
																				si++;
																				sockets_finished = si >= num_connections;

																				if (sockets_finished)
																				{
																								std::cout << "Sockets conectados: " << sockets.size() << std::endl;
																				}
																}
												}
								}
								else
								{
												int i = 0;
												for (i = 0; i < nfds; i++)
												{
																int fd = events[i].data.fd;
																int evs = events[i].events;

																if (fd == 0 && (evs & EPOLLIN))
																{
																				int bytes_read = read(fd, buffer, 512);
																				buffer[bytes_read] = 0;


																				std::string cm = buffer;

																				cm.erase(std::remove(cm.begin(), cm.end() ,'\n'), cm.end());

																				if (cm == "kill")
																				{
																								running = 0;
																				} else if (cm == "stop"){
																								execute_test = false;
																				} else if (cm == "start"){
																								execute_test = true;
																				}
																				else if (strncmp(cm.c_str(), "irc", 3)==0) {

																								std::map<int, struct cstatus*>::iterator it;

																								for (it = sockets.begin(); it != sockets.end(); it++)
																								{
																												write(it->second->socket, &buffer[3], strlen(&buffer[3]));

																								}

																				}

																				else if (strncmp(cm.c_str(), "ctcp", 4)==0) {

																								std::map<int, struct cstatus*>::iterator it;

																								char nick[16];
																								char msg[32];

																								sscanf(buffer, "ctcp %s %s\n", nick, msg);

																								int w = sprintf(buffer, "privmsg %s :%c%s%c\n", nick, 1, msg, 1);

																								for (it = sockets.begin(); it != sockets.end(); it++)
																								{
																												write(it->second->socket, buffer, w);

																								}
																				}
																				else {
																								std::cout << "Comando desconocido" << std::endl;
																				}
																}
																else if (fd != 0 && ((evs & EPOLLHUP) || (evs & EPOLLRDHUP)))
																{
																				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
																				delete sockets[fd];
																				sockets.erase(fd);
																				fds.erase(std::find(fds.begin(), fds.end(), fd));
																				log("Socket %d cerrado\n", fd);
																				if (sockets.empty()) {
																								std::cout << "ATENCION: Todos los sockets se han cerrado!. Se paran los tests" << std::endl;
																								execute_test = false;
																								log("ATENCIÃ“N: Todos los sockets han sido cerrados por el servidor");
																				}
																}
																else if ((evs & EPOLLIN))
																{

																				struct cstatus& cs = *sockets[fd];



																				int bytes_read = read(fd, buffer, 512);
																				buffer[bytes_read] = 0;

																				sbuffer << buffer;

																				log(buffer);


																				if (cs.status !=0)
																				{

																								while (sbuffer.getline(line, 512))
																								{
																												if (sbuffer.fail())
																												{
																																break;
																												}
																												std::stringstream ls(line);
																												ls >> server >> raw >> nick >> msg;


																												if (cs.status == 2 && raw == "005" && nick == cs.nick)
																												{
																																std::string com;
																																com = "JOIN " + cs.channel + "\n";
																																write(fd, com.c_str(), com.size());
																																cs.status = 4;
																												}
																												if (cs.status >= 2)
																												{

																																if (server == "PING")
																																{
																																				std::string pong = "PONG";
																																				pong = pong + " " + raw + "\n";
																																				write(fd, pong.c_str(), pong.size());

																																}
																																else if (raw == "PRIVMSG" || raw == "PONG" )
																																{
																																				long int u ;

																																				sscanf(msg.c_str(), ":%ld\n", &u);


																																				current_test.ms = (int)(current_timestamp() - u);
																																				current_test.msgs++;
																																				//current_test.ms = (int)(current_test.ms / current_test.msgs);

																																}

																												}

																								}
																								if (sbuffer.eof())
																								{
																												sbuffer.clear();
																												sbuffer.str("");
																								}
																								else if (sbuffer.fail())
																								{
																												sbuffer.clear();
																												sbuffer.str(line);
																								}
																				}
																}
																else
																{
																				//printf("%d\n", evs);
																}
												}
								}
				}

				fclose(data);
				if (LOG.enable)
								fclose(LOG.logfile);

				std::map<int, struct cstatus*>::iterator it;

				for (it = sockets.begin(); it != sockets.end(); it++)
				{
								close(it->second->socket);
								delete it->second;

				}
				close(epoll_fd);
				freeaddrinfo(hostinfo);

				return 0;
}
