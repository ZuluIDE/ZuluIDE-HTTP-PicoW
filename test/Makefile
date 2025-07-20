# Run basic unit tests for the zuluide-http-picow

all: url_decode_test
	./url_decode_test

url_decode_test: url_decode_test.cpp ../src/url_decode.cpp
	g++ -Wall -Wextra -g -ggdb -o $@ -I ../src $^
