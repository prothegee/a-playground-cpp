_build_trade() {
    mkdir -p bin;

    g++ ./src/trade_consumer.cc -o ./bin/trade_consumer.cc.o \
        -Wall -Wextra \
        -std=c++23 \
        -g \
        -pthread \
        -march=native;

    g++ ./src/trade_engine.cc -o ./bin/trade_engine.cc.o \
        -Wall -Wextra \
        -std=c++23 \
        -g \
        -pthread \
        -march=native;

}
