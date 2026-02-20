_build_trade() {
    g++ ./src/trade_consumer.cc -o ./trade_consumer.cc.o \
        -std=c++23 \
        -g \
        -pthread \
        -march=native;

    g++ ./src/trade_engine.cc -o ./trade_engine.cc.o \
        -std=c++23 \
        -g \
        -pthread \
        -march=native;

}
