main : main.o lst_timer.o http_conn.o log.o sql_connection_pool.o
	g++ main.o lst_timer.o http_conn.o log.o sql_connection_pool.o -o main -lpthread -lmysqlclient

main.o : main.cpp
	g++ -c main.cpp

lst_timer.o : lst_timer.cpp
	g++ -c lst_timer.cpp

http_conn.o : http_conn.cpp
	g++ -c http_conn.cpp

log.o : log.cpp
	g++ -c log.cpp

sql_connection_pool.o : sql_connection_pool.cpp
	g++ -c sql_connection_pool.cpp

# .PHONY:clean
clean:
	rm -f *.o main