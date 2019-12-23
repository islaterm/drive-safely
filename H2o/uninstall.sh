# Unregisters the h2o module and cleans the output of the compilation.
#
# Author: Ignacio Slater Muñoz
# Version 1.0.10.2

sudo rmmod h2o.ko
sudo rm /dev/h2o
make clean