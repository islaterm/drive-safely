# Compiles and registers the h2o module.
#
# Author: Ignacio Slater Muñoz
# Version 0.3

make
if [ -f h2o.ko ]; then
  sudo mknod /dev/h2o c 60 0
  sudo chmod a+rw /dev/h2o
  sudo insmod h2o.ko
  dmesg | tail
fi
