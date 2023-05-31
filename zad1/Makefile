.PHONY: all clean

LDFLAGS = -lboost_program_options -lpthread

all: sikradio-sender sikradio-receiver

sikradio-sender.o: sikradio-sender.cpp
	g++ -c -Wall -O2 -o $@ $<

sikradio-receiver.o: sikradio-receiver.cpp
	g++ -c -Wall -O2 -o $@ $<

utils.o: utils.cpp
	g++ -c -Wall -O2 -o $@ $<

ui.o: ui.cpp
	g++ -c -Wall -O2 -o $@ $<

sikradio-sender: sikradio-sender.o utils.o
	g++ -o $@ $^ $(LDFLAGS)

sikradio-receiver: sikradio-receiver.o utils.o ui.o
	g++ -o $@ $^ $(LDFLAGS)

clean:
	rm -rf sikradio-sender sikradio-receiver *.o
