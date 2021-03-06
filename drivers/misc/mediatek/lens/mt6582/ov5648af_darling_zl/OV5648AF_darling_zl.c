/*
 * MD218A voice coil motor driver
 *
 *
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include "OV5648AF_darling_zl.h"
#include "../camera/kd_camera_hw.h"

#define LENS_I2C_BUSNUM 1
static struct i2c_board_info __initdata kd_lens_dev={ I2C_BOARD_INFO("OV5648AF_DARLING_ZL", 0x1a)};


#define OV5648AF_DARLING_ZL_DRVNAME "OV5648AF_DARLING_ZL"
#define OV5648AF_DARLING_ZL_VCM_WRITE_ID           0x18

#define OV5648AF_DARLING_ZL_DEBUG
#ifdef OV5648AF_DARLING_ZL_DEBUG
#define OV5648AF_DARLING_ZL_DB printk
#else
#define OV5648AF_DARLING_ZL_DB(x,...)
#endif

static spinlock_t g_OV5648AF_DARLING_ZL_SpinLock;

static struct i2c_client * g_pstOV5648AF_DARLING_ZL_I2Cclient = NULL;

static dev_t g_OV5648AF_DARLING_ZL_devno;
static struct cdev * g_pOV5648AF_DARLING_ZL_CharDrv = NULL;
static struct class *actuator_class = NULL;

static int  g_s4OV5648AF_DARLING_ZL_Opened = 0;
static long g_i4MotorStatus = 0;
static long g_i4Dir = 0;
static unsigned long g_u4OV5648AF_DARLING_ZL_INF = 0;
static unsigned long g_u4OV5648AF_DARLING_ZL_MACRO = 1023;
static unsigned long g_u4TargetPosition = 0;
static unsigned long g_u4CurrPosition   = 0;

static int g_sr = 3;

#if 0
extern s32 mt_set_gpio_mode(u32 u4Pin, u32 u4Mode);
extern s32 mt_set_gpio_out(u32 u4Pin, u32 u4PinOut);
extern s32 mt_set_gpio_dir(u32 u4Pin, u32 u4Dir);
#endif

static int s4OV5648AF_DARLING_ZL_ReadReg(unsigned short * a_pu2Result)
{
    int  i4RetValue = 0;
    char pBuff[2];

    i4RetValue = i2c_master_recv(g_pstOV5648AF_DARLING_ZL_I2Cclient, pBuff , 2);

    if (i4RetValue < 0) 
    {
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] I2C read failed!! \n");
        return -1;
    }

    *a_pu2Result = (((u16)pBuff[0]) << 4) + (pBuff[1] >> 4);

    return 0;
}

static int s4OV5648AF_DARLING_ZL_WriteReg(u16 a_u2Data)
{
    int  i4RetValue = 0;

    char puSendCmd[2] = {(char)(a_u2Data >> 4) , (char)(((a_u2Data & 0xF) << 4)+g_sr)};

    //OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] g_sr %d, write %d \n", g_sr, a_u2Data);
    g_pstOV5648AF_DARLING_ZL_I2Cclient->ext_flag |= I2C_A_FILTER_MSG;
    i4RetValue = i2c_master_send(g_pstOV5648AF_DARLING_ZL_I2Cclient, puSendCmd, 2);
	
    if (i4RetValue < 0) 
    {
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] I2C send failed!! \n");
        return -1;
    }

    return 0;
}

inline static int getOV5648AF_DARLING_ZL_Info(__user stOV5648AF_DARLING_ZL_MotorInfo * pstMotorInfo)
{
    stOV5648AF_DARLING_ZL_MotorInfo stMotorInfo;
    stMotorInfo.u4MacroPosition   = g_u4OV5648AF_DARLING_ZL_MACRO;
    stMotorInfo.u4InfPosition     = g_u4OV5648AF_DARLING_ZL_INF;
    stMotorInfo.u4CurrentPosition = g_u4CurrPosition;
    stMotorInfo.bIsSupportSR      = TRUE;

	if (g_i4MotorStatus == 1)	{stMotorInfo.bIsMotorMoving = 1;}
	else						{stMotorInfo.bIsMotorMoving = 0;}

	if (g_s4OV5648AF_DARLING_ZL_Opened >= 1)	{stMotorInfo.bIsMotorOpen = 1;}
	else						{stMotorInfo.bIsMotorOpen = 0;}

    if(copy_to_user(pstMotorInfo , &stMotorInfo , sizeof(stOV5648AF_DARLING_ZL_MotorInfo)))
    {
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] copy to user failed when getting motor information \n");
    }

    return 0;
}

inline static int moveOV5648AF_DARLING_ZL(unsigned long a_u4Position)
{
    int ret = 0;
    
    if((a_u4Position > g_u4OV5648AF_DARLING_ZL_MACRO) || (a_u4Position < g_u4OV5648AF_DARLING_ZL_INF))
    {
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] out of range \n");
        return -EINVAL;
    }

    if (g_s4OV5648AF_DARLING_ZL_Opened == 1)
    {
        unsigned short InitPos;
        ret = s4OV5648AF_DARLING_ZL_ReadReg(&InitPos);
	    
        spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);
        if(ret == 0)
        {
            OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Init Pos %6d \n", InitPos);
            g_u4CurrPosition = (unsigned long)InitPos;
        }
        else
        {		
            g_u4CurrPosition = 0;
        }
        g_s4OV5648AF_DARLING_ZL_Opened = 2;
        spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);
    }

    if (g_u4CurrPosition < a_u4Position)
    {
        spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);	
        g_i4Dir = 1;
        spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);	
    }
    else if (g_u4CurrPosition > a_u4Position)
    {
        spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);	
        g_i4Dir = -1;
        spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);			
    }
    else										{return 0;}

    spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);    
    g_u4TargetPosition = a_u4Position;
    spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);	

    //OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] move [curr] %d [target] %d\n", g_u4CurrPosition, g_u4TargetPosition);

            spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);
            g_sr = 3;
            g_i4MotorStatus = 0;
            spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);	
		
            if(s4OV5648AF_DARLING_ZL_WriteReg((unsigned short)g_u4TargetPosition) == 0)
            {
                spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);		
                g_u4CurrPosition = (unsigned long)g_u4TargetPosition;
                spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);				
            }
            else
            {
                OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] set I2C failed when moving the motor \n");			
                spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);
                g_i4MotorStatus = -1;
                spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);				
            }

    return 0;
}

inline static int setOV5648AF_DARLING_ZL_Inf(unsigned long a_u4Position)
{
    spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);
    g_u4OV5648AF_DARLING_ZL_INF = a_u4Position;
    spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);	
    return 0;
}

inline static int setOV5648AF_DARLING_ZL_Macro(unsigned long a_u4Position)
{
    spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);
    g_u4OV5648AF_DARLING_ZL_MACRO = a_u4Position;
    spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);	
    return 0;	
}

////////////////////////////////////////////////////////////////
static long OV5648AF_DARLING_ZL_Ioctl(
struct file * a_pstFile,
unsigned int a_u4Command,
unsigned long a_u4Param)
{
    long i4RetValue = 0;

    switch(a_u4Command)
    {
        case OV5648AF_DARLING_ZL_IOC_G_MOTORINFO :
            i4RetValue = getOV5648AF_DARLING_ZL_Info((__user stOV5648AF_DARLING_ZL_MotorInfo *)(a_u4Param));
        break;

        case OV5648AF_DARLING_ZL_IOC_T_MOVETO :
            i4RetValue = moveOV5648AF_DARLING_ZL(a_u4Param);
        break;
 
        case OV5648AF_DARLING_ZL_IOC_T_SETINFPOS :
            i4RetValue = setOV5648AF_DARLING_ZL_Inf(a_u4Param);
        break;

        case OV5648AF_DARLING_ZL_IOC_T_SETMACROPOS :
            i4RetValue = setOV5648AF_DARLING_ZL_Macro(a_u4Param);
        break;
		
        default :
      	    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] No CMD \n");
            i4RetValue = -EPERM;
        break;
    }

    return i4RetValue;
}

//Main jobs:
// 1.check for device-specified errors, device not ready.
// 2.Initialize the device if it is opened for the first time.
// 3.Update f_op pointer.
// 4.Fill data structures into private_data
//CAM_RESET
static int OV5648AF_DARLING_ZL_Open(struct inode * a_pstInode, struct file * a_pstFile)
{
    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] OV5648AF_DARLING_ZL_Open - Start\n");

    spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);

    if(g_s4OV5648AF_DARLING_ZL_Opened)
    {
        spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] the device is opened \n");
        return -EBUSY;
    }

    g_s4OV5648AF_DARLING_ZL_Opened = 1;
		
    spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);

    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] OV5648AF_DARLING_ZL_Open - End\n");

    return 0;
}

//Main jobs:
// 1.Deallocate anything that "open" allocated in private_data.
// 2.Shut down the device on last close.
// 3.Only called once on last time.
// Q1 : Try release multiple times.
static int OV5648AF_DARLING_ZL_Release(struct inode * a_pstInode, struct file * a_pstFile)
{
    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] OV5648AF_DARLING_ZL_Release - Start\n");

    if (g_s4OV5648AF_DARLING_ZL_Opened)
    {
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] feee \n");
        g_sr = 5;
	    s4OV5648AF_DARLING_ZL_WriteReg(200);
        msleep(10);
	    s4OV5648AF_DARLING_ZL_WriteReg(100);
        msleep(10);
            	            	    	    
        spin_lock(&g_OV5648AF_DARLING_ZL_SpinLock);
        g_s4OV5648AF_DARLING_ZL_Opened = 0;
        spin_unlock(&g_OV5648AF_DARLING_ZL_SpinLock);

    }

    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] OV5648AF_DARLING_ZL_Release - End\n");

    return 0;
}

static const struct file_operations g_stOV5648AF_DARLING_ZL_fops = 
{
    .owner = THIS_MODULE,
    .open = OV5648AF_DARLING_ZL_Open,
    .release = OV5648AF_DARLING_ZL_Release,
    .unlocked_ioctl = OV5648AF_DARLING_ZL_Ioctl
};

inline static int Register_OV5648AF_DARLING_ZL_CharDrv(void)
{
    struct device* vcm_device = NULL;

    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Register_OV5648AF_DARLING_ZL_CharDrv - Start\n");

    //Allocate char driver no.
    if( alloc_chrdev_region(&g_OV5648AF_DARLING_ZL_devno, 0, 1,OV5648AF_DARLING_ZL_DRVNAME) )
    {
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Allocate device no failed\n");

        return -EAGAIN;
    }

    //Allocate driver
    g_pOV5648AF_DARLING_ZL_CharDrv = cdev_alloc();

    if(NULL == g_pOV5648AF_DARLING_ZL_CharDrv)
    {
        unregister_chrdev_region(g_OV5648AF_DARLING_ZL_devno, 1);

        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Allocate mem for kobject failed\n");

        return -ENOMEM;
    }

    //Attatch file operation.
    cdev_init(g_pOV5648AF_DARLING_ZL_CharDrv, &g_stOV5648AF_DARLING_ZL_fops);

    g_pOV5648AF_DARLING_ZL_CharDrv->owner = THIS_MODULE;

    //Add to system
    if(cdev_add(g_pOV5648AF_DARLING_ZL_CharDrv, g_OV5648AF_DARLING_ZL_devno, 1))
    {
        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Attatch file operation failed\n");

        unregister_chrdev_region(g_OV5648AF_DARLING_ZL_devno, 1);

        return -EAGAIN;
    }

    actuator_class = class_create(THIS_MODULE, "actuatordrv4");
    if (IS_ERR(actuator_class)) {
        int ret = PTR_ERR(actuator_class);
        OV5648AF_DARLING_ZL_DB("Unable to create class, err = %d\n", ret);
        return ret;            
    }

    vcm_device = device_create(actuator_class, NULL, g_OV5648AF_DARLING_ZL_devno, NULL, OV5648AF_DARLING_ZL_DRVNAME);

    if(NULL == vcm_device)
    {
        return -EIO;
    }
    
    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Register_OV5648AF_DARLING_ZL_CharDrv - End\n");    
    return 0;
}

inline static void Unregister_OV5648AF_DARLING_ZL_CharDrv(void)
{
    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Unregister_OV5648AF_DARLING_ZL_CharDrv - Start\n");

    //Release char driver
    cdev_del(g_pOV5648AF_DARLING_ZL_CharDrv);

    unregister_chrdev_region(g_OV5648AF_DARLING_ZL_devno, 1);
    
    device_destroy(actuator_class, g_OV5648AF_DARLING_ZL_devno);

    class_destroy(actuator_class);

    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Unregister_OV5648AF_DARLING_ZL_CharDrv - End\n");    
}

//////////////////////////////////////////////////////////////////////

static int OV5648AF_DARLING_ZL_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int OV5648AF_DARLING_ZL_i2c_remove(struct i2c_client *client);
static const struct i2c_device_id OV5648AF_DARLING_ZL_i2c_id[] = {{OV5648AF_DARLING_ZL_DRVNAME,0},{}};   
struct i2c_driver OV5648AF_DARLING_ZL_i2c_driver = {                       
    .probe = OV5648AF_DARLING_ZL_i2c_probe,                                   
    .remove = OV5648AF_DARLING_ZL_i2c_remove,                           
    .driver.name = OV5648AF_DARLING_ZL_DRVNAME,                 
    .id_table = OV5648AF_DARLING_ZL_i2c_id,                             
};  

#if 0 
static int OV5648AF_DARLING_ZL_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info) {         
    strcpy(info->type, OV5648AF_DARLING_ZL_DRVNAME);                                                         
    return 0;                                                                                       
}      
#endif 
static int OV5648AF_DARLING_ZL_i2c_remove(struct i2c_client *client) {
    return 0;
}

/* Kirby: add new-style driver {*/
static int OV5648AF_DARLING_ZL_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int i4RetValue = 0;

    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] OV5648AF_DARLING_ZL_i2c_probe\n");

    /* Kirby: add new-style driver { */
    g_pstOV5648AF_DARLING_ZL_I2Cclient = client;
    
    g_pstOV5648AF_DARLING_ZL_I2Cclient->addr = OV5648AF_DARLING_ZL_VCM_WRITE_ID >> 1;
    
    //Register char driver
    i4RetValue = Register_OV5648AF_DARLING_ZL_CharDrv();

    if(i4RetValue){

        OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] register char device failed!\n");

        return i4RetValue;
    }

    spin_lock_init(&g_OV5648AF_DARLING_ZL_SpinLock);

    OV5648AF_DARLING_ZL_DB("[OV5648AF_DARLING_ZL] Attached!! \n");

    return 0;
}

static int OV5648AF_DARLING_ZL_probe(struct platform_device *pdev)
{
    return i2c_add_driver(&OV5648AF_DARLING_ZL_i2c_driver);
}

static int OV5648AF_DARLING_ZL_remove(struct platform_device *pdev)
{
    i2c_del_driver(&OV5648AF_DARLING_ZL_i2c_driver);
    return 0;
}

static int OV5648AF_DARLING_ZL_suspend(struct platform_device *pdev, pm_message_t mesg)
{
    return 0;
}

static int OV5648AF_DARLING_ZL_resume(struct platform_device *pdev)
{
    return 0;
}

// platform structure
static struct platform_driver g_stOV5648AF_DARLING_ZL_Driver = {
    .probe		= OV5648AF_DARLING_ZL_probe,
    .remove	= OV5648AF_DARLING_ZL_remove,
    .suspend	= OV5648AF_DARLING_ZL_suspend,
    .resume	= OV5648AF_DARLING_ZL_resume,
    .driver		= {
        .name	= "lens_actuator4",
        .owner	= THIS_MODULE,
    }
};

static struct platform_device actuator_dev4 = {
	.name		  = "lens_actuator4",
	.id		  = -1,
};

static int __init OV5648AF_DARLING_ZL_i2C_init(void)
{
    i2c_register_board_info(LENS_I2C_BUSNUM, &kd_lens_dev, 1);
    platform_device_register(&actuator_dev4);
	
    if(platform_driver_register(&g_stOV5648AF_DARLING_ZL_Driver)){
        OV5648AF_DARLING_ZL_DB("failed to register OV5648AF_DARLING_ZL driver\n");
        return -ENODEV;
    }

    return 0;
}

static void __exit OV5648AF_DARLING_ZL_i2C_exit(void)
{
	platform_driver_unregister(&g_stOV5648AF_DARLING_ZL_Driver);
}

module_init(OV5648AF_DARLING_ZL_i2C_init);
module_exit(OV5648AF_DARLING_ZL_i2C_exit);

MODULE_DESCRIPTION("OV5648AF_DARLING_ZL lens module driver");
MODULE_AUTHOR("KY Chen <ky.chen@Mediatek.com>");
MODULE_LICENSE("GPL");


