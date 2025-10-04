CXXFLAGS=-I$(HOME)/blockingqueue/rapidjson/include
LD=g++
LDFLAGS=-lcurl

all: client

client: client.o
	$(LD) $< -o $@ $(LDFLAGS)

client.o: client.cpp
	$(LD) $(CXXFLAGS) -c client.cpp -o client.o

clean:
	rm -f client client.o