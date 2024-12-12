if [["$1" == "x86"]]; then
    ./configure && make clean && make
else
    ./configure --host=aarch64-qcom-linux  && make clean && make
fi
