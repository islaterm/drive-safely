Este ejemplo es una adaptacion del tutorial incluido
(archivo "device drivers tutorial.pdf") y bajado de:
http://www.freesoftwaremagazine.com/articles/drivers_linux

---

Guia rapida:

Lo siguiente se debe realizar parados en
el directorio en donde se encuentra este README.txt

+ Compilacion (preferentemente en modo usuario):
% make
...
% ls
... h2o.ko ...

+ Instalacion (en modo root)

# mknod /dev/h2o c 60 0
# chmod a+rw /dev/h2o
# insmod h2o.ko
# dmesg | tail
...
[...........] Inserting h2o module
#

+ Testing (preferentemente en modo usuario)

Ud. necesitara crear 4 shells independientes.  Luego
siga las instrucciones del enunciado de la tarea 3 de 2019-2

+ Desinstalar el modulo

# rmmod h2o.ko
#
