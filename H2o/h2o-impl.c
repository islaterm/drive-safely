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
 * Example usage:
 *
 * | Shell 1                   | Shell 2                   | Shell 3                   | Shell 4              |
 * | ------------------------- | ------------------------- | ------------------------- | -------------------- |
 * | ``$ echo abc > /dev/h2o`` |                           |                           |                      |
 * |                           | ``$ echo def > /dev/h2o`` |                           |                      |
 * |                           |                           | ``$ echo ghi > /dev/h2o`` |                      |
 * |                           |                           |                           | ``$ cat < /dev/h20`` |
 * | ``$``                     | ``$``                     |                           | ``abc``              |
 * |                           |                           |                           | ``ghi``              |
 * |                           | ``$ echo jkl > /dev/h2o`` |                           |                      |
 * |                           |                           |                           |                      |
 * | ``$ echo mno > /dev/h2o`` |                           |                           |                      |
 * |                           |                           | ``$ echo pqr > /dev/h20`` |                      |
 * | ``$``                     |                           | ``$``                     | ``mno``              |
 * |                           |                           |                           | ``pqr``              |
 * |                           |                           |                           | ``<Control+C>``      |
 * |                           |                           |                           | ``$``                |
 *
 * @author Ignacio Slater Muñoz
 * @version 1.0.2
 * @since 1.0
 */

/* Necessary includes for device drivers */
#include <linux/init.h>
/* #include <linux/config.h> */
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>   /* O_ACCMODE */
#include <linux/uaccess.h> /* copy_from/to_user */

#include "kmutex.h"

MODULE_LICENSE("Dual BSD/GPL");

/* Declaration of h2o.c functions */
static int h2o_open(struct inode *inode, struct file *filp);
static int h2o_release(struct inode *inode, struct file *filp);
static ssize_t h2o_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t h2o_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);

void h2o_exit(void);
int h2o_init(void);

/* Structure that declares the usual file */
/* access functions */
struct file_operations h2o_fops = {
  read : h2o_read,
  write : h2o_write,
  open : h2o_open,
  release : h2o_release
};

/* Declaration of the init and exit functions */
module_init(h2o_init);
module_exit(h2o_exit);

/*** El driver para lecturas sincronas *************************************/

#define TRUE 1
#define FALSE 0

/* Global variables of the driver */

int h2oMajor = 60; /* Major number */

/* Buffer to store data */
#define MAX_SIZE 10

static char *h2o_buffer;
static int in, out, size;

/* El mutex y la condicion para h2o */
static KMutex mutex;
static KCondition cond;

int h2o_init(void)
{
  int rc;

  /* Registering device */
  rc = register_chrdev(h2oMajor, "h2o", &h2o_fops);
  if (rc < 0)
  {
    printk(
        "<1>h2o: cannot obtain major number %d\n", h2oMajor);
    return rc;
  }

  in = out = size = 0;
  m_init(&mutex);
  c_init(&cond);

  /* Allocating h2o_buffer */
  h2o_buffer = kmalloc(MAX_SIZE, GFP_KERNEL);
  if (h2o_buffer == NULL)
  {
    h2o_exit();
    return -ENOMEM;
  }
  memset(h2o_buffer, 0, MAX_SIZE);

  printk("<1>Inserting h2o module\n");
  return 0;
}

void h2o_exit(void)
{
  /* Freeing the major number */
  unregister_chrdev(h2oMajor, "h2o");

  /* Freeing buffer h2o */
  if (h2o_buffer)
  {
    kfree(h2o_buffer);
  }

  printk("<1>Removing h2o module\n");
}

static int h2o_open(struct inode *inode, struct file *filp)
{
  char *mode = filp->f_mode & FMODE_WRITE ? "write" : filp->f_mode & FMODE_READ ? "read" : "unknown";
  printk("<1>open %p for %s\n", filp, mode);
  return 0;
}

static int h2o_release(struct inode *inode, struct file *filp)
{
  printk("<1>release %p\n", filp);
  return 0;
}

static ssize_t h2o_read(struct file *filp, char *buf,
                         size_t ucount, loff_t *f_pos)
{
  ssize_t count = ucount;

  printk("<1>read %p %ld\n", filp, count);
  m_lock(&mutex);

  while (size == 0)
  {
    /* si no hay nada en el buffer, el lector espera */
    if (c_wait(&cond, &mutex))
    {
      printk("<1>read interrupted\n");
      count = -EINTR;
      goto epilog;
    }
  }

  if (count > size)
  {
    count = size;
  }

  /* Transfiriendo datos hacia el espacio del usuario */
  int k;
  for (k = 0; k < count; k++)
  {
    if (copy_to_user(buf + k, h2o_buffer + out, 1) != 0)
    {
      /* el valor de buf es una direccion invalida */
      count = -EFAULT;
      goto epilog;
    }
    printk("<1>read byte %c (%d) from %d\n",
           h2o_buffer[out], h2o_buffer[out], out);
    out = (out + 1) % MAX_SIZE;
    size--;
  }

epilog:
  c_broadcast(&cond);
  m_unlock(&mutex);
  return count;
}

static ssize_t h2o_write(struct file *filp, const char *buf,
                          size_t ucount, loff_t *f_pos)
{
  ssize_t count = ucount;

  printk("<1>write %p %ld\n", filp, count);
  m_lock(&mutex);
  int k;
  for (k = 0; k < count; k++)
  {
    while (size == MAX_SIZE)
    {
      /* si el buffer esta lleno, el escritor espera */
      if (c_wait(&cond, &mutex))
      {
        printk("<1>write interrupted\n");
        count = -EINTR;
        goto epilog;
      }
    }

    if (copy_from_user(h2o_buffer + in, buf + k, 1) != 0)
    {
      /* el valor de buf es una direccion invalida */
      count = -EFAULT;
      goto epilog;
    }
    printk("<1>write byte %c (%d) at %d\n",
           h2o_buffer[in], h2o_buffer[in], in);
    in = (in + 1) % MAX_SIZE;
    size++;
    c_broadcast(&cond);
  }

epilog:
  m_unlock(&mutex);
  return count;
}
