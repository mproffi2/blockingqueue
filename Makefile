# Sequential client
client: client.o
<TAB>g++ client.o -o client -lcurl

client.o: client.cpp
<TAB>g++ -I$(PWD)/rapidjson/include -c client.cpp -o client.o

# Parallel client
parallel_client: parallel_level_client.o
<TAB>g++ parallel_level_client.o -o parallel_client -lcurl -lpthread

parallel_level_client.o: parallel_level_client.cpp
<TAB>g++ -I$(PWD)/rapidjson/include -c parallel_level_client.cpp -o parallel_level_client.o
