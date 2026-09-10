/**************************************************************************/
/*********** 该i2c驱动从rtt的i2c驱动框架移植而来,剔除了device层 ***********/
/**** 考虑了并发情况,考虑到片内资源单独跑os内核的情况可以用,并不占资源 ****/
/**************************************************************************/




#include "drv_soft_i2c.h"

#include "main.h"


// 使用互斥锁会被高优先级任务抢占
// 偶发性的导致i2c信号传输一半的时候就跑去做其他的
// 最后导致读写i2c数据不对进而BQ芯片驱动的CRC通不过
//static struct rt_mutex mutex1 ={0};

//static rt_uint32_t level;


/**
 * @brief 初始化软件I2C总线锁。
 * @param 无。
 * @return 无返回值。
 */
static void I2C1_LockInit(void)
{
    //rt_mutex_init(&mutex1, "i2c1_lock", RT_IPC_FLAG_FIFO);
}

/**
 * @brief 进入软件I2C传输临界区。
 * @param 无。
 * @return 无返回值。
 */
static void I2C1_Lock(void)
{
    //rt_mutex_take(&mutex1, RT_WAITING_FOREVER);
	__disable_irq();
}

/**
 * @brief 退出软件I2C传输临界区。
 * @param 无。
 * @return 无返回值。
 */
static void I2C1_Unlock(void)
{
    //rt_mutex_release(&mutex1);

    __enable_irq();
}

// 适用于72MHZ
/**
 * @brief 执行软件I2C使用的近似微秒忙等待延时。
 * @param us 延时时长，单位为微秒。
 * @return 无返回值。
 */
static void delay_us(uint32_t us)
{
	volatile uint16_t i = 0;
	
	while(us--)
	{
		i = 120; //自己定义
		while(i-- != 0U);
	}
}

// i2c总线句柄
struct I2C_BusTypeDef i2c1 = 
{
	.gpiox = I2C1_SCL_GPIO_Port,
	.sda_gpio_pin = I2C1_SDA_Pin,
	.scl_gpio_pin = I2C1_SCL_Pin,
	.retries = 3,
	.udelay = (void (*)(uint32_t))delay_us,
	.lockInit = I2C1_LockInit,
	.lock = I2C1_Lock,
	.unlock = I2C1_Unlock,
};


/**
 * @brief 初始化软件I2C总线GPIO和锁。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static void I2C_BusHardwareInitialize(struct I2C_BusTypeDef *bus)
{
    gpio_init(bus->gpiox, GPIO_MODE_OUT_OD, GPIO_OSPEED_50MHZ,
              bus->scl_gpio_pin | bus->sda_gpio_pin);

    gpio_bit_set(bus->gpiox, bus->scl_gpio_pin);
    gpio_bit_set(bus->gpiox, bus->sda_gpio_pin);

    if (bus->lockInit)bus->lockInit();
}


/**
 * @brief 将SDA引脚配置为开漏输出。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void SDA_SetOutMode(struct I2C_BusTypeDef *bus)
{
    gpio_init(bus->gpiox, GPIO_MODE_OUT_OD, GPIO_OSPEED_50MHZ, bus->sda_gpio_pin);
}

// stm32的IO口结构输出模式下是没有关断输入部分的肖特基触发器,数据依然会读入输入寄存器,故不用设置SDA输入模式
// 但为了保险起见最好还是写上,也为了方便将该驱动移植到其他平台或者HAL库上
/**
 * @brief 将SDA引脚配置为浮空输入。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void SDA_SetInMode(struct I2C_BusTypeDef *bus)
{
    gpio_init(bus->gpiox, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, bus->sda_gpio_pin);
}

/**
 * @brief 读取SDA引脚当前逻辑电平。
 * @param bus I2C总线对象指针。
 * @return SDA 为高电平时返回 1，为低电平时返回 0。
 */
static inline uint8_t GET_SDA(struct I2C_BusTypeDef *bus)
{
	return (SET == gpio_input_bit_get(bus->gpiox, bus->sda_gpio_pin)) ? 1U : 0U;
}

/**
 * @brief 将SDA线拉低。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void SDA_L(struct I2C_BusTypeDef *bus)
{
    gpio_bit_reset(bus->gpiox, bus->sda_gpio_pin);
}

/**
 * @brief 释放SDA线为高电平。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void SDA_H(struct I2C_BusTypeDef *bus)
{
    gpio_bit_set(bus->gpiox, bus->sda_gpio_pin);
}

/**
 * @brief 将SCL线拉低。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void SCL_L(struct I2C_BusTypeDef *bus)
{
    gpio_bit_reset(bus->gpiox, bus->scl_gpio_pin);
}

/**
 * @brief 释放SCL线为高电平。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void SCL_H(struct I2C_BusTypeDef *bus)
{
    gpio_bit_set(bus->gpiox, bus->scl_gpio_pin);
}

/**
 * @brief 产生软件I2C起始条件。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void I2C_Start(struct I2C_BusTypeDef *bus)
{
	SDA_L(bus);
	bus->udelay(1);
	SCL_L(bus);
}

/**
 * @brief 产生软件I2C重复起始条件。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void I2C_Restart(struct I2C_BusTypeDef *bus)
{
	SDA_H(bus);
	SCL_H(bus);
	bus->udelay(1);
	SDA_L(bus);
	bus->udelay(1);
	SCL_L(bus);
}

/**
 * @brief 产生软件I2C停止条件。
 * @param bus I2C总线对象指针。
 * @return 无返回值。
 */
static inline void I2C_Stop(struct I2C_BusTypeDef *bus)
{
	SDA_L(bus);
	bus->udelay(1);
	SCL_H(bus);
	bus->udelay(1);
	SDA_H(bus);
	bus->udelay(1);
}

/**
 * @brief 读取从机对当前字节的ACK状态。
 * @param bus I2C总线对象指针。
 * @return 收到从机 ACK 时返回 1，收到 NACK 时返回 0。
 */
static inline uint8_t I2C_WaitACK(struct I2C_BusTypeDef *bus)
{
	uint8_t ack;

	SDA_H(bus);
	SDA_SetInMode(bus);
	bus->udelay(1);
	SCL_H(bus);
	bus->udelay(1);
	ack = !GET_SDA(bus);    /* ACK : SDA pin is pulled low */
	SCL_L(bus);
	SDA_SetOutMode(bus);
	I2C_INFO("%s", ack ? "ACK" : "NACK");

	return ack;
}
/**
 * @brief 在接收字节后向从机发送ACK或NACK。
 * @param bus I2C总线对象指针。
 * @param ack 非 0 时发送 ACK，0 时发送 NACK。
 * @return 无返回值。
 */
static inline void I2C_SendAckOrNack(struct I2C_BusTypeDef *bus, int ack)
{
	if (ack) {
		SDA_L(bus);
	} else {
		SDA_H(bus);
	}
	bus->udelay(1);
	SCL_H(bus);
	bus->udelay(1);
	SCL_L(bus);
}
/**
 * @brief 按位发送一个字节并读取从机ACK。
 * @param bus I2C总线对象指针。
 * @param data 要发送的 8 位数据。
 * @return 从机应答时返回 1，未应答时返回 0。
 */
static uint8_t I2C_WriteByte(struct I2C_BusTypeDef *bus, uint8_t data)
{
	uint8_t mask;
	
	for (mask = 0x80; mask != 0; mask >>= 1)
	{
		SCL_L(bus);
		data & mask ? SDA_H(bus) : SDA_L(bus);
		bus->udelay(1);
		SCL_H(bus);
		bus->udelay(1);
	}
	SCL_L(bus);
	bus->udelay(1);

	return I2C_WaitACK(bus);
}
/**
 * @brief 按位读取一个字节。
 * @param bus I2C总线对象指针。
 * @return 返回读取到的8位数据。
 */
static uint8_t I2C_ReadByte(struct I2C_BusTypeDef *bus)
{
	uint8_t mask;
	uint8_t data = 0;

	SDA_H(bus);
	bus->udelay(1);
	SDA_SetInMode(bus);
	for (mask = 0x80; mask != 0; mask >>= 1) 
	{
		SCL_H(bus);
		bus->udelay(1);
		if (GET_SDA(bus)) data |= mask;
		SCL_L(bus);
		bus->udelay(1);
	}
	SDA_SetOutMode(bus);

	return data;
}
/**
 * @brief 按照消息描述发送全部数据字节。
 * @param bus I2C总线对象指针。
 * @param msg I2C消息描述对象。
 * @return 返回实际发送或接收的字节数。
 */
static uint16_t I2C_SendBytes(struct I2C_BusTypeDef *bus, struct I2C_MessageTypeDef *msg)
{
	uint8_t ret;
	const uint8_t *ptr = msg->buf;
	uint16_t bytes = 0, count = msg->tLen;
	
	while (count > 0)
	{
		if (msg->flags & I2C_CONTROL_BYTE && I2C_WriteByte(bus, msg->cByte) == 0) // 发送控制字节
		{
			I2C_WARNING("send bytes: NACK.");
			break;
		}

		ret = msg->flags & I2C_SAME_BYTE ? I2C_WriteByte(bus, msg->sByte) : I2C_WriteByte(bus, *ptr) , ptr++;
		
		if ((ret > 0) || (msg->flags & I2C_IGNORE_NACK && (ret == 0)))
		{
			count --;			
			bytes ++;
		}
		else if (ret == 0)
		{
			I2C_WARNING("send bytes: NACK.");
			break;
		}
	}

	return bytes;
}

/**
 * @brief 按照消息描述接收全部数据字节。
 * @param bus I2C总线对象指针。
 * @param msg I2C消息描述对象。
 * @return 返回实际发送或接收的字节数。
 */
static uint16_t I2C_RecvBytes(struct I2C_BusTypeDef *bus, struct I2C_MessageTypeDef *msg)
{
	uint8_t val;
	uint8_t *ptr = msg->buf;
	uint16_t bytes = 0, count = msg->tLen;

	while (count > 0)
	{
		val = I2C_ReadByte(bus);
		*ptr = val;
		bytes ++;
		
		ptr++;
		count--;

		I2C_INFO("recieve bytes: 0x%02x, %s",
							val, (msg->flags & I2C_NO_READ_ACK) ?
							"(No ACK/NACK)" : (count ? "ACK" : "NACK"));

		if (!(msg->flags & I2C_NO_READ_ACK))
		{
			I2C_SendAckOrNack(bus, count);
		}
	}

	return bytes;
}

/**
 * @brief 发送从机地址并按配置重试。
 * @param bus I2C总线对象指针。
 * @param addr I2C地址字节。
 * @param retries 地址发送重试次数。
 * @return 从机应答地址时返回 1，重试后仍未应答时返回 0。
 */
static uint8_t I2C_SendAddress(struct I2C_BusTypeDef *bus, uint8_t addr, uint32_t retries)
{
	uint8_t i, ret = 0;

	for (i = 0; i <= retries; i++)
	{
		ret = I2C_WriteByte(bus, addr);
		if (ret == 1)
		{
			I2C_INFO("response ok.");
			break;
		}
		else if (i == retries)
		{
			I2C_WARNING("no response, please check slave device.");
			break;
		}
		I2C_WARNING("no response, attempt to resend the address. number:%d.", i);
		I2C_Stop(bus);
		bus->udelay(1);
		I2C_Start(bus);
	}

	return ret;
}

/**
 * @brief 根据7位或10位地址和读写方向发送地址阶段。
 * @param bus I2C总线对象指针。
 * @param msg I2C消息描述对象。
 * @return 地址阶段成功时返回 1，失败时返回 0。
 */
static uint8_t I2C_BitSendAddress(struct I2C_BusTypeDef *bus, struct I2C_MessageTypeDef *msg)
{
	uint8_t ret, retries, addr1, addr2;
	uint8_t flags = msg->flags;
	uint8_t ignore_nack = msg->flags & I2C_IGNORE_NACK;


	retries = ignore_nack ? 0 : bus->retries;

	if (flags & I2C_ADDR_10BIT)
	{
		addr1 = 0xf0 | ((msg->addr >> 7) & 0x06);
		addr2 = msg->addr & 0xff;

		I2C_INFO("addr1: %d, addr2: %d", addr1, addr2);

		ret = I2C_SendAddress(bus, addr1, retries);
		if ((ret != 1) && !ignore_nack)
		{
			I2C_WARNING("NACK: sending first addr");
			return 0;
		}

		ret = I2C_WriteByte(bus, addr2);
		if ((ret != 1) && !ignore_nack)
		{
			I2C_WARNING("NACK: sending second addr");
			return 0;
		}
		if (flags & I2C_RD)
		{
			I2C_INFO("send repeated start condition");
			I2C_Restart(bus);
			addr1 |= 0x01;
			ret = I2C_SendAddress(bus, addr1, retries);
			if ((ret != 1) && !ignore_nack)
			{
				I2C_ERROR("NACK: sending repeated addr");
				return 0;
			}
		}
	}
	else
	{
		/* 7-bit addr */
		addr1 = msg->addr << 1;
		if (flags & I2C_RD)
				addr1 |= 1;
		ret = I2C_SendAddress(bus, addr1, retries);
		if ((ret != 1) && !ignore_nack)
				return 0;
	}

	return 1;
}

/* 传输消息,如果传输成功返回总的消息数量,失败返回0 */
/**
 * @brief 依次执行一组软件 I2C 消息的起始、地址、数据和停止阶段。
 * @param bus I2C总线对象指针。
 * @param msgs I2C消息数组。
 * @param num msgs 数组中的消息数量。
 * @return 返回成功完成的消息数量，失败返回0。
 */
uint32_t I2C_TransferMessages(struct I2C_BusTypeDef *bus, struct I2C_MessageTypeDef msgs[], uint32_t num)
{
	struct I2C_MessageTypeDef *msg;
	uint32_t i, ret = 0;
	uint8_t ignore_nack;

	if (NULL == bus || NULL == msgs || num == 0)return ret;

	if (bus->lock) bus->lock();

	for (i = 0; i < num; i++)
	{
		msg = &msgs[i];
		ignore_nack = msg->flags & I2C_IGNORE_NACK;
		if (!(msg->flags & I2C_NO_START))
		{
			if (i)
			{
				I2C_Restart(bus);
			}
			else
			{
				I2C_INFO("send start condition");
				I2C_Start(bus);
			}
			ret = I2C_BitSendAddress(bus, msg);
			if ((ret != 1) && !ignore_nack)
			{
				I2C_WARNING("receive NACK from device addr 0x%02x msg %d", msgs[i].addr, i);
				goto out;
			}
		}
		if (msg->flags & I2C_RD)
		{
			ret = I2C_RecvBytes(bus, msg);
			msg->rLen = ret;
			I2C_INFO("read %d byte%s", ret, ret == 1 ? "" : "s");
		}
		else
		{
			ret = I2C_SendBytes(bus, msg);	
			msg->rLen = ret;
			I2C_INFO("write %d byte%s", ret, ret == 1 ? "" : "s");				
			if (msg->rLen != msg->tLen)
			{
				ret = 0;
				goto out;
			}
			
		}
	}
	ret = i;

out:
	if (!(msg->flags & I2C_NO_STOP))
	{
		I2C_INFO("send stop condition");
		I2C_Stop(bus);
	}

	if(bus->unlock) bus->unlock();
	
	return ret;
}


/**
 * @brief 初始化全局软件I2C总线实例。
 * @param 无。
 * @return 初始化成功返回 0。
 */
int I2C_BusInitialize(void)
{
	I2C_BusHardwareInitialize(&i2c1);
	
	return 0;
}



