///
/// Linux module to produce molecules of H2O.
///
/// An H2O molecule needs 2 hydrogen particles and 1 particle of oxygen.
/// - Hydrogen particles are provided by performing a write operation on ``/dev/h2o``.
/// - Oxygen particles are provided by performing a read operation on ``/dev/h2o``.
///
/// Both writing and reading tasks must wait until a molecule is created to finish, but a
/// read operation can be interrupted with the ``<Control+C>`` signal.
///
/// When the molecule is created, the read operation returns the concatenation of the
/// parameters given to the write command in FIFO order.
///
/// Example usage:
/// | Shell 1                   | Shell 2                   | Shell 3                   | Shell 4              |
/// | ------------------------- | ------------------------- | ------------------------- | -------------------- |
/// | ``$ echo abc > /dev/h2o`` |                           |                           |                      |
/// |                           | ``$ echo def > /dev/h2o`` |                           |                      |
/// |                           |                           | ``$ echo ghi > /dev/h2o`` |                      |
/// |                           |                           |                           | ``$ cat < /dev/h20`` |
/// | ``$``                     | ``$``                     |                           | ``abc``              |
/// |                           |                           |                           | ``ghi``              |
/// |                           | ``$ echo jkl > /dev/h2o`` |                           |                      |
/// |                           |                           |                           |                      |
/// | ``$ echo mno > /dev/h2o`` |                           |                           |                      |
/// |                           |                           | ``$ echo pqr > /dev/h20`` |                      |
/// | ``$``                     |                           | ``$``                     | ``mno``              |
/// |                           |                           |                           | ``pqr``              |
/// |                           |                           |                           | ``<Control+C>``      |
/// |                           |                           |                           | ``$``                |
///
/// \author Ignacio Slater Muñoz
/// \version 1.0.9.11
/// \since 1.0

#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-label"

#pragma region : Necessary includes for device drivers
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> // printk()
#include <linux/slab.h>   // kmalloc()
#include <linux/fs.h>     // everything...
#include <linux/errno.h>  // error codes
#include <linux/types.h>  // size_t
#include <linux/proc_fs.h>
#include <linux/fcntl.h>   // O_ACCMODE
#include <linux/uaccess.h> // copy_from/to_user
#pragma endregion

#include "kmutex.h"

MODULE_LICENSE("Dual BSD/GPL");
#pragma region : global declarations
/// Size of the buffer to store data
#define MAX_SIZE 8192
#pragma endregion

#pragma region : Declaration of h2o.c functions
/// Opens the H2O module.
/// Each time the module is opened, it's file descriptor is different.
///
/// \param inode
///     a struct containing the characteristics of the file.
/// \param pFile
///     the file descriptor.
/// \returns 0 if the operation was succesfull; an error code otherwise.
static int openH2O(struct inode *inode, struct file *pFile);

/// Closes the H2O module.
/// Each time the module is opened, it's file descriptor is different.
///
/// \param inode
///     a struct containing the characteristics of the file.
/// \param pFile
///     the file descriptor.
///
/// \returns 0 if the operation was succesfull; an error code otherwise.
static int releaseH2O(struct inode *inode, struct file *pFile);

/// Reads a fragment of the file.
/// If there are bytes remaining to be read from ``pFilePos``, then the function returns
/// ``count`` and ``pFilePos`` is moved to the first unread byte.
///
/// \param pFile
///     the file descriptor.
/// \param buf
///     the direction where the read data should be placed.
/// \param count
///     the maximum number of bytes to read.
/// \param pFilePos
///     the position from where the bytes should be read.
///
/// \returns the number of bytes read, 0 if it reaches the file's end or an error code if
///     the read operation fails.
static ssize_t readH2O(struct file *pFile, char *buf, size_t count, loff_t *pFilePos);

/// Adds a hydrogen particle to the file.
/// If the number of bytes to be written is larger than the maximum buffer size, then only
/// the bytes that doesn't exceed the buffer size are written to the file and an error
/// code is returned.
///
/// \param pFile
///     the file descriptor.
/// \param buf
///     the data to be written in the file.
/// \param count
///     the maximum number of bytes to write.
/// \param pFilePos
///     the position from where the bytes should be written.
static ssize_t writeH2O(struct file *pFile, const char *buf, size_t count,
                        loff_t *pFilePos);

/// Unregisters the H2O driver and releases it's buffer.
void exitH2O(void);

/// Registers the H2O driver and initializes it's buffer.
int initH2O(void);
#pragma endregion

/// Structure that declares the usual file access functions.
struct file_operations pH2OFileOperations = {
  read : readH2O,
  write : writeH2O,
  open : openH2O,
  release : releaseH2O
};

/// Major number
int h2oMajor = 60;

#pragma region : local variables
static char *bufferH2O;
static int in, out, size;

/// H2O's mutex
static KMutex mutex;
/// Mutex conditions
static KCondition cond, waitingMolecule, waitingHydrogen;
static int hydrogens, oxygens;
#pragma endregion

#pragma region : Declaration of the init and exit functions
module_init(initH2O);
module_exit(exitH2O);
#pragma endregion

int initH2O(void)
{
  int returnCode;

  /* Registering device */
  returnCode = register_chrdev(h2oMajor, "h2o", &pH2OFileOperations);
  if (returnCode < 0)
  {
    printk("<1>h2o: cannot obtain major number %d\n", h2oMajor);
    return returnCode;
  }
  oxygens = 0;
  hydrogens = 0;
  in = out = size = 0;
  m_init(&mutex);
  c_init(&cond);
  c_init(&waitingMolecule);
  c_init(&waitingHydrogen);

  /* Allocating bufferH2O */
  bufferH2O = kmalloc(MAX_SIZE, GFP_KERNEL);
  if (bufferH2O == NULL)
  {
    exitH2O();
    return -ENOMEM;
  }
  memset(bufferH2O, 0, MAX_SIZE);

  printk("<1>Inserting h2o module\n");
  return 0;
}

void exitH2O(void)
{
  /* Freeing the major number */
  unregister_chrdev(h2oMajor, "h2o");

  /* Freeing buffer h2o */
  if (bufferH2O)
  {
    kfree(bufferH2O);
  }

  printk("<1>Removing h2o module\n");
}

static int openH2O(struct inode *inode, struct file *pFile)
{
  char *mode = pFile->f_mode & FMODE_WRITE
                   ? "write"
                   : pFile->f_mode & FMODE_READ
                         ? "read"
                         : "unknown";
  printk("<1>open %p for %s\n", pFile, mode);
  return 0;
}

static int releaseH2O(struct inode *inode, struct file *pFile)
{
  printk("<1>release %p\n", pFile);
  return 0;
}

static ssize_t readH2O(struct file *pFile, char *buf, size_t ucount, loff_t *pFilePos)
{
  int k;
  ssize_t count = ucount;

  printk("<1>read %p %ld\n", pFile, count);
  m_lock(&mutex);
  printk("DEBUG:readH2O:  lock (((aquired))) %s\n", bufferH2O);

  oxygens++;
  printk("DEBUG:readH2O:  there's %d oxygens %s\n", oxygens, bufferH2O);
  try:
  {
    while (hydrogens < 2)
    {
      printk("DEBUG:readH2O:  Not enough hydrogens. Going to sleep %s\n", bufferH2O);
      // The procedure waits if there's not enough hydrogens
      if (c_wait(&waitingHydrogen, &mutex))
      {
        printk("<1>read interrupted\n");
        count = -EINTR;
        goto finally;
      }
      printk("DEBUG:readH2O:  I'm awake %s\n", bufferH2O);
    }

    if (count > size)
    {
      count = size;
    }

    /* Transfiriendo datos hacia el espacio del usuario */
    for (k = 0; k < count; k++)
    {
      if (copy_to_user(buf + k, bufferH2O + out, 1) != 0)
      {
        /* el valor de buf es una direccion invalida */
        count = -EFAULT;
        goto finally;
      }
      printk("<1>read byte %c (%d) from %d\n",
             bufferH2O[out], bufferH2O[out], out);
      out = (out + 1) % MAX_SIZE;
      size--;
    }
  }

  finally:
  {
    c_broadcast(&cond);
    printk("DEBUG:readH2O:  Broadcasting %s\n", bufferH2O);
    m_unlock(&mutex);
    printk("DEBUG:readH2O:  (((Unlocked))) %s\n", bufferH2O);
    return count;
  }
}

static ssize_t writeH2O(struct file *pFile, const char *buf, size_t ucount,
                        loff_t *pFilePos)
{
  int k;
  ssize_t count = ucount;

  printk("<1>write %p %ld\n", pFile, count);
  m_lock(&mutex);
  printk("DEBUG:writeH2O: lock (((aquired))) %s\n", buf);
  try:
  {
    while (hydrogens >= 2)
    {
      printk("DEBUG:writeH2O: Too much hydrogen. Going to sleep %s\n", buf);
      // The process waits if there's not enough oxygens to form a molecule
      if (c_wait(&waitingMolecule, &mutex))
      {
        printk("<1>write interrupted\n");
        count = -EINTR;
        goto finally;
      }
      printk("DEBUG:writeH2O: I'm awake %s\n", buf);
    }

    for (k = 0; k < count; k++)
    {
      if (copy_from_user(bufferH2O + in, buf + k, 1) != 0)
      {
        // The buffer's adress is invalid
        count = -EFAULT;
        goto finally;
      }
      printk("<1>write byte %c (%d) at %d\n", bufferH2O[in], bufferH2O[in], in);
      in = (in + 1) % MAX_SIZE;
      size++;
    }
    hydrogens++;
    c_broadcast(&waitingHydrogen);
    printk("DEBUG:writeH2O: There's %d hydrogens %s\n", hydrogens, buf);
    while (hydrogens < 2)
    {
      printk("DEBUG:writeH2O: Not enough hydrogens. Going to sleep %s\n", buf);
      // The process waits if there's not enough oxygens to form a molecule
      if (c_wait(&waitingHydrogen, &mutex))
      {
        printk("<1>write interrupted\n");
        count = -EINTR;
        goto finally;
      }
      printk("DEBUG:writeH2O: I'm awake %s\n", buf);
    }
    while (oxygens < 1)
    {
      printk("DEBUG:writeH2O: Not enough oxygens. Going to sleep %s\n", buf);
      // The process waits if there's not enough oxygens to form a molecule
      if (c_wait(&cond, &mutex))
      {
        printk("<1>write interrupted\n");
        count = -EINTR;
        goto finally;
      }
      printk("DEBUG:writeH2O: I'm awake %s\n", buf);
      c_broadcast(&cond);
    }
    if (hydrogens > 0 && oxygens > 0)
    {
      hydrogens--;
      oxygens--;
      c_broadcast(&waitingMolecule);
    }
  }
  finally:
  {
    m_unlock(&mutex);
    printk("DEBUG:writeH2O: (((unlocked))) %s\n", buf);
    return count;
  }
}