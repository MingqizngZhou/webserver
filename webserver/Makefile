main : main.o lst_timer.o http_conn.o log.o
	g++ main.o lst_timer.o http_conn.o log.o -o main -lpthread

main.o : main.cpp
	g++ -c main.cpp

lst_timer.o : lst_timer.cpp
	g++ -c lst_timer.cpp

http_conn.o : http_conn.cpp
	g++ -c http_conn.cpp

log.o : log.cpp
	g++ -c log.cpp
