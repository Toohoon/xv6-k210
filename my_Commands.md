docker run -ti --rm -v ./xv6-k210:/xv6 --privileged=true docker.educg.net/cg/os-contest:2024p6 /bin/bash

docker run -ti --rm -v ./testsuits-for-oskernel-main/riscv-syscalls-testing:/testing --privileged=true docker.educg.net/cg/os-contest:2024p6 /bin/bash

make my_set
make run
