# sequential build
client: client.o
    g++ client.o -o client -lcurl

client.o: client.cpp
    g++ -I$(PWD)/rapidjson/include -c client.cpp -o client.o

# parallel client build
parallel_client: parallel_level_client.o
    g++ parallel_level_client.o -o parallel_client -lcurl -lpthread

parallel_level_client.o: parallel_level_client.cpp
    g++ -I$(PWD)/rapidjson/include -c parallel_level_client.cpp -o parallel_level_client.o
