# H2O Module

This example is an adaptation of the tutorial obtained from:
http://www.freesoftwaremagazine.com/articles/drivers_linux

## Quick guide:

The following commands should be executed in the same directory as this ``README.md``
file.

### Compilation

```bash
$ make
(...)
$ if [ -f h2o.ko ]; then
$   echo true
$ fi
>>  true
```

### Instalation

```bash
$ sudo mknod /dev/h2o c 60 0
$ sudo chmod a+rw /dev/h2o
$ sudo insmod h2o.ko
$ dmesg | tail
(...)
>>  Inserting h2o module
```

### Testing

For this you will need to create 4 different shells.
To test the module execute the example showed in the document: 
``[CC4302] Tarea 3 - 2019P - Mateu.pdf``

### Uninstall

```bash
$ sudo rmmod h2o.ko
```
