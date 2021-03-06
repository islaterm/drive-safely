/**
* Linux module to produce molecules of H2O.
*
* An H2O molecule needs 2 hydrogen particles and 1 particle of oxygen.
* - Hydrogen particles are provided by performing a write operation on ``/dev/h2o``.
* - Oxygen particles are provided by performing a read operation on ``/dev/h2o``.
*
* Both writing and reading tasks must wait until a molecule is created to finish, but a
* read operation can be interrupted with the ``<Control+C>`` signal.
*
* When the molecule is created, the read operation returns the concatenation of the
* parameters given to the write command in FIFO order.
*
* @author   Ignacio Slater Muñoz
* @version  1.0-release
* @since    1.0
*/

#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma region : Header
#pragma region Necessary includes for device drivers

#include <linux/init.h>
/* #include <linux/config.h> */
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/uaccess.h> /* copy_from/to_user */

#pragma endregion

#include "kmutex.h"

#pragma region Global variables of the driver.
/// Major number
int majorH2O = 60;
/// Buffer to store data.
#define MAX_SIZE 8
#pragma endregion

#pragma region Local variables.
static char *bufferH2O;
static int in, out, size, k;
static KMutex mutex;
static KCondition waitingHydrogen, waitingMolecule;
#pragma endregion
#pragma region Declaration of h2o.c functions

/**
 * Opens the H2O module.
 * Each time the module is opened, it's file descriptor is different.
 *
 * @param inode a struct containing the characteristics of the file.
 * @param pFile the file descriptor.
 *
 * @returns 0 if the operation was succesfull; an error code otherwise.
 */
static int openH2O(struct inode *inode, struct file *pFile);

/**
 * Closes the H2O module.
 *
 * Each time the module is opened, it's file descriptor is different.
 *
 * @param inode a struct containing the characteristics of the file.
 * @param pFile the file descriptor.
 *
 * @returns 0 if the operation was succesfull; an error code otherwise.
 */
static int releaseH2O(struct inode *inode, struct file *pFile);

/**
 * Reads a fragment of the file.
 *
 * If there are bytes remaining to be read from ``pFilePos``, then the function returns
 * ``count`` and ``pFilePos`` is moved to the first unread byte.
 *
 * @param pFile     the file descriptor.
 * @param buf       the direction where the read data should be placed.
 * @param count     the maximum number of bytes to read.
 * @param pFilePos  the position from where the bytes should be read.
 *
 * @returns the number of bytes read, 0 if it reaches the file's end or an error code if
 *          the read operation fails.
 */
static ssize_t readH2O(struct file *pFile, char *buf, size_t ucount, loff_t *pFilePos);

/**
 * Adds a hydrogen particle to the file.
 * If the number of bytes to be written is larger than the maximum buffer size, then only
 * the bytes that doesn't exceed the buffer size are written to the file and an error
 * code is returned.
 *
 * @param pFile
 *     the file descriptor.
 * @param buf
 *     the data to be written in the file.
 * @param count
 *     the maximum number of bytes to write.
 * @param pFilePos
 *     the position from where the bytes should be written.
 */
static ssize_t writeH2O(struct file *pFile, const char *buf, size_t ucount,
                        loff_t *pFilePos);

/// Unregisters the H2O driver and releases it's buffer.
void exitH2O(void);

/// Registers the H2O driver and initializes it's buffer.
int initH2O(void);

#pragma endregion
#pragma Helper functions

/// Ends a writing process and returns a code indicating if it was successful or not.
static ssize_t endWrite(int code);

static ssize_t endRead(ssize_t code);

static ssize_t end(ssize_t code);

static ssize_t waitHydrogen(void);

static ssize_t createMolecule(char *buf);

static ssize_t waitRelease(void);

static ssize_t writeBytes(const char *buf);

static ssize_t produceHydrogen(ssize_t count, const char *buf);

#pragma endregion
#pragma endregion

MODULE_LICENSE("Dual BSD/GPL");
// Declaration of the init and exit functions
module_init(initH2O);
module_exit(exitH2O);

/// Structure that declares the usual file access functions.
struct file_operations fileOperations = {
    .read =  readH2O,
    .write =  writeH2O,
    .open =  openH2O,
    .release =  releaseH2O
};

#pragma region : Implementation

int initH2O(void) {
  int response;
  // Registering device
  response = register_chrdev(majorH2O, "h2o", &fileOperations);
  if (response < 0) {
    printk("ERROR:initH2O: Cannot obtain major number %d\n", majorH2O);
    return response;
  }

  in = out = size = 0;
  m_init(&mutex);
  c_init(&waitingHydrogen);
  c_init(&waitingMolecule);

  // Allocating bufferH2O
  bufferH2O = kmalloc(MAX_SIZE, GFP_KERNEL);
  if (bufferH2O == NULL) {
    exitH2O();
    return -ENOMEM;
  }
  memset(bufferH2O, 0, MAX_SIZE);

  printk("INFO:initH2O: Inserting h2o module\n");
  return 0;
}

void exitH2O(void) {
  // Freeing the major number
  unregister_chrdev(majorH2O, "h2o");

  // Freeing buffer h2o
  if (bufferH2O) {
    kfree(bufferH2O);
  }

  printk("INFO:exitH2O: Removing h2o module\n");
}

static int openH2O(struct inode *inode, struct file *pFile) {
  char *mode = pFile->f_mode & FMODE_WRITE ?
               "write" : pFile->f_mode & FMODE_READ ?
                         "read" : "unknown";
  printk("INFO:openH2O: Open %p for %s\n", pFile, mode);
  return 0;
}

static int releaseH2O(struct inode *inode, struct file *pFile) {
  printk("INFO:releaseH2O: release %p\n", pFile);
  return 0;
}

#pragma region Read/Write

static ssize_t readH2O(struct file *pFile, char *buf, size_t ucount, loff_t *pFilePos) {
  ssize_t count = ucount;
  ssize_t response;

  printk("INFO:readH2O: Read %p %ld\n", pFile, count);
  m_lock(&mutex);
  if ((response = waitHydrogen()) != 0) {
    return endRead(response);
  }
  if ((response = createMolecule(buf)) != 0) {
    return endRead(response);
  }
  return endRead(count);
}

static ssize_t writeH2O(struct file *pFile, const char *buf,
                        size_t ucount, loff_t *pFilePos) {
  ssize_t count = ucount;
  ssize_t response;

  printk("INFO:writeH2O: Write %p %ld\n", pFile, count);
  m_lock(&mutex);
  while (size == MAX_SIZE) {
    c_wait(&waitingMolecule, &mutex);
  }
  if ((response = produceHydrogen(count, buf) != 0)) {
    return endWrite(response);
  }
  c_wait(&waitingMolecule, &mutex);
  return endWrite(count);
}

static ssize_t produceHydrogen(ssize_t count, const char *buf) {
  ssize_t response;
  for (k = 0; k < count; k++) {
    if ((response = waitRelease()) != 0) {
      return endWrite(response);
    }
    if ((response = writeBytes(buf)) != 0) {
      return endWrite(response);
    }
  }
  return 0;
}

static ssize_t writeBytes(const char *buf) {
  if (copy_from_user(bufferH2O + in, buf + k, 1) != 0) {
    return -EFAULT;
  }
  printk("INFO:writeH2O:writeBytes: byte %c (%d) at %d\n", bufferH2O[in], bufferH2O[in],
         in);
  in = (in + 1) % MAX_SIZE;
  size++;
  c_broadcast(&waitingHydrogen);
  return 0;
}

static ssize_t waitRelease(void) {
  while (size == MAX_SIZE) {
    if (c_wait(&waitingHydrogen, &mutex)) {
      printk("INFO:writeH2O:waitRelease: Interrupted\n");
      return -EINTR;
    }
  }
  return 0;
}

#pragma endregion

static ssize_t createMolecule(char *buf) {
  for (k = 0; k < MAX_SIZE; k++) {
    if (copy_to_user(buf + k, bufferH2O + out, 1) != 0) {
      printk("ERROR:readH2O:createMolecule: Invalid adress");
      return -EFAULT;
    }
    printk("INFO:readH2O:createMolecule: Read byte %c (%d) from %d\n", bufferH2O[out],
           bufferH2O[out], out);
    out = (out + 1) % MAX_SIZE;
    size--;
  }
  c_broadcast(&waitingMolecule);
  return 0;
}

static ssize_t waitHydrogen(void) {
  while (size < MAX_SIZE) {
    if (c_wait(&waitingHydrogen, &mutex)) {
      printk("INFO:readH2O:waitHydrogen: Interrupted.\n");
      return -EINTR;
    }
  }
  return 0;
}

#pragma region : ending functions

static ssize_t endRead(ssize_t code) {
  c_broadcast(&waitingHydrogen);
  return end(code);
}

static ssize_t endWrite(int code) {
  return end(code);
}

static ssize_t end(ssize_t code) {
  m_unlock(&mutex);
  return code;
}

#pragma endregion
#pragma endregion