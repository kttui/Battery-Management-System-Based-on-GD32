#include "bms_utils.h"


/*
 * 函数作用：在有序数组中二分查找 target，找到返回下标，未找到返回 -1。
 */
/**
 * @brief 在有序数组中二分查找 target。
 * @param nums 有序数组。
 * @param left 查找区间左边界。
 * @param right 查找区间右边界。
 * @param target 待查找或比较的目标值。
 * @return 找到时返回匹配下标，未找到时返回 -1。
 */
int binarySearch(uint16_t *nums, uint8_t left, uint8_t right, uint16_t target)
{
    while (left <= right) 
	{ 
		// 注意
        int mid = (right + left) / 2;
			
        if(nums[mid] == target)
		{
            return mid; 
		}
        else if (nums[mid] < target)
		{
            left = mid + 1; // 注意
		}
        else if (nums[mid] > target)
		{
            right = mid - 1; // 注意
        }
	}
    return -1;
}

// 查找一个数在数组中的右侧边界(二分法)
// start_pos：起始位置
// end_pos：结束位置
// 返回-1：表示不存在这个数
/*
 * 函数作用：二分查找小于等于 target 的右侧边界下标。
 */
/**
 * @brief 二分查找小于等于 target 的最右侧元素下标。
 * @param nums 有序数组。
 * @param start_pos 查找区间起始位置。
 * @param end_pos 查找区间结束位置。
 * @param target 待查找或比较的目标值。
 * @return 返回右侧边界下标；区间内无满足条件的元素时返回 -1。
 */
int right_bound(uint16_t *nums, uint8_t start_pos, uint8_t end_pos, uint16_t target)
{
	uint16_t left = start_pos;
	uint16_t right = end_pos;
		
    while (left < right)
	{
        int mid = (left + right) / 2;
		
        if (nums[mid] == target) 
		{
            left = mid + 1; // 注意
        } 
		else if (nums[mid] < target) 
		{
            left = mid + 1;
        }
		else if (nums[mid] > target) 
		{
            right = mid;
        }
    }
	if ((left - 1) < start_pos)
	{
		return -1;
	}
    return left - 1; // 注意
}

// 查找一个数在数组中的左侧边界(二分法)
// start_pos：起始位置
// end_pos：结束位置
// 返回-1：表示不存在这个数
/*
 * 函数作用：二分查找大于等于 target 的左侧边界下标。
 */
/**
 * @brief 二分查找大于等于 target 的最左侧元素下标。
 * @param nums 有序数组。
 * @param start_pos 查找区间起始位置。
 * @param end_pos 查找区间结束位置。
 * @param target 待查找或比较的目标值。
 * @return 返回左侧边界下标；区间内无满足条件的元素时返回 -1。
 */
int left_bound(uint16_t *nums, uint16_t start_pos, uint16_t end_pos, uint16_t target)
{
	uint16_t left = start_pos;
	uint16_t right = end_pos;
	
    while (left < right) 
	{ 
		// 注意
        int mid = (left + right) / 2;
		
        if (nums[mid] == target) 
		{
            right = mid;
        }
		else if (nums[mid] < target) 
		{
            left = mid + 1;
        } 
		else if (nums[mid] > target) 
		{
            right = mid; // 注意
        }
    }
	
	if (left > end_pos)
	{
		return -1;
	}
    return left;
}



/*
 * 函数作用：对 float 数组进行冒泡排序，排序结果从小到大。
 */
/**
 * @brief 对 float 数组执行升序冒泡排序。
 * @param a 待排序浮点数组。
 * @param n 数组元素数量。
 * @return 无返回值。
 */
void BubbleFloat(float a[], uint32_t n)
{
	float t;
    uint32_t i, j;
      
    for (i = 1; i < n; i++)
    {
        for (j = 0; j < n-i; j++)
        {
            if (a[j] > a[j+1])
            {
                t = a[j];
                a[j] = a[j+1];
                a[j+1] = t;
            }
        }
    }
}

/*
void bubble(int a[],int n)
{
    int i,j,t;
    
    for (i = 0; i < n-1; i++)
    {
        for (j = 0; j < n-i-1; j++)
        {
            if (a[j] > a[j+1])
            {
                t = a[j];
                a[j] = a[j+1];
                a[j+1] = t;
            }
        }
    }
}
*/







/*
 * 函数作用：比较两个 int8_t 数据，用于通用排序接口。
 */
/**
 * @brief 比较两个 int8_t 元素的大小。
 * @param e1 第一个待比较元素。
 * @param e2 第二个待比较元素。
 * @return 大于0表示前者应排在后者之后，其他值表示无需交换。
 */
int cmp_int8_t(const void *e1, const void *e2)
{
    return *(int8_t *)e1 - *(int8_t *)e2;
}

/*
 * 函数作用：比较两个 uint8_t 数据，用于通用排序接口。
 */
/**
 * @brief 比较两个 uint8_t 元素的大小。
 * @param e1 第一个待比较元素。
 * @param e2 第二个待比较元素。
 * @return 大于0表示前者应排在后者之后，其他值表示无需交换。
 */
int cmp_uint8_t(const void *e1, const void *e2)
{
    return *(uint8_t *)e1 - *(uint8_t *)e2;
}

/*
 * 函数作用：比较两个 int16_t 数据，用于通用排序接口。
 */
/**
 * @brief 比较两个 int16_t 元素的大小。
 * @param e1 第一个待比较元素。
 * @param e2 第二个待比较元素。
 * @return 大于0表示前者应排在后者之后，其他值表示无需交换。
 */
int cmp_int16_t(const void *e1, const void *e2)
{
    return *(int16_t *)e1 - *(int16_t *)e2;
}

/*
 * 函数作用：比较两个 uint16_t 数据，用于通用排序接口。
 */
/**
 * @brief 比较两个 uint16_t 元素的大小。
 * @param e1 第一个待比较元素。
 * @param e2 第二个待比较元素。
 * @return 大于0表示前者应排在后者之后，其他值表示无需交换。
 */
int cmp_uint16_t(const void *e1, const void *e2)
{
    return *(uint16_t *)e1 - *(uint16_t *)e2;
}

/*
 * 函数作用：比较两个 float 数据，前者大于后者时返回 1。
 */
/**
 * @brief 比较两个 float 元素的大小。
 * @param e1 第一个待比较元素。
 * @param e2 第二个待比较元素。
 * @return 前者大于后者时返回 1，否则返回 0。
 */
int cmp_float(const void *e1, const void *e2)
{
	if (*(float *)e1 > *(float *)e2)
	{
		return 1;
	}

    return 0;    
}

/*
 * 函数作用：比较两个 double 数据，前者大于后者时返回 1。
 */
/**
 * @brief 比较两个 double 元素的大小。
 * @param e1 第一个待比较元素。
 * @param e2 第二个待比较元素。
 * @return 前者大于后者时返回 1，否则返回 0。
 */
int cmp_double(const void *e1, const void *e2)
{
	if (*(double *)e1 > *(double *)e2)
	{
		return 1;
	}

    return 0;
}


/*
 * 函数作用：按字节交换两个任意类型元素的内容，width 表示元素大小。
 */
/**
 * @brief 按字节交换两个任意类型元素的内容。
 * @param buf1 第一个待交换元素地址。
 * @param buf2 第二个待交换元素地址。
 * @param width 单个元素的字节宽度。
 * @return 无返回值。
 */
void swap(uint8_t *buf1, uint8_t *buf2, uint32_t width)
{
	uint8_t temp;
    uint32_t i;
    
    for (i = 0; i < width; i++)
    {
        temp = *buf1;
        *buf1 = *buf2;
        *buf2 = temp;
        buf1++;
        buf2++;
    }
}


// base：	基地址
// sz:		要排序元素个数
// width:	单个元素的宽度
// cmp:		不明确类型的情况下,两个数据的对比结果必须由用户完成
//			如果e1比e2大则cmp应返回大于0的数,反之则返回小于等于0的数
/*
 * 函数作用：通用冒泡排序函数，通过 cmp 回调比较任意类型元素。
 */
/**
 * @brief 使用比较回调对任意类型数组执行升序冒泡排序。
 * @param base 待排序数组首地址。
 * @param sz 待排序元素数量。
 * @param width 单个元素的字节宽度。
 * @param cmp 元素比较回调。
 * @return 无返回值。
 */
void BubbleSort(void *base, uint32_t sz, uint32_t width, int (*cmp)(void *e1, void *e2))
{
    uint32_t i = 0, j = 0;

    for (i = 1; i < sz; i++)
    {
        for (j = 0; j < sz - i; j++)
        {
            if (cmp((uint8_t *)base + j * width, (uint8_t *)base + (j + 1) * width) > 0)
            {
                swap((uint8_t *)base + j * width, (uint8_t *)base + (j + 1) * width, width);
            }
        }
    }
}





/*

下面是任意类型数据冒排序的测试代码



#define BMS_CELL_MAX	5
#define BMS_TEMP_MAX	2


typedef struct
{
	float CellVoltage; 	// 电芯电压
	uint8_t CellNumber;	// 电芯的编号
}BMS_CellDataTypedef;

typedef struct
{
	float CellTemp[BMS_TEMP_MAX];					// 采样温度,温度数据会从小到大排序
	float BatteryVoltage;							// 电池总电压
	float BatteryCurrent;							// 电池组电流
	BMS_CellDataTypedef CellData[BMS_CELL_MAX]; 	// 电芯电压,电压数据会从小到大排序
}BMS_SampleDataTypedef;


int cmp1(void *e1, void *e2)
{
	float temp1, temp2;
	
	temp1 = (*(BMS_CellDataTypedef *)e1).CellVoltage;
	temp2 = (*(BMS_CellDataTypedef *)e2).CellVoltage;

	if (temp1 > temp2)
	{
		return 1;
	}

    return 0;
}

 
int main()
{
	uint8_t index;
	
	BMS_SampleDataTypedef SampleData;
	
	SampleData.CellData[0].CellNumber = 0;
	SampleData.CellData[1].CellNumber = 1;
	SampleData.CellData[2].CellNumber = 2;
	SampleData.CellData[3].CellNumber = 3;
	SampleData.CellData[4].CellNumber = 4;
	
	
	SampleData.CellData[0].CellVoltage = 3.32;
	SampleData.CellData[1].CellVoltage = 3.12;
	SampleData.CellData[2].CellVoltage = 3.59;
	SampleData.CellData[3].CellVoltage = 3.57;
	SampleData.CellData[4].CellVoltage = 4.20;
	
	
	BubbleSort(SampleData.CellData, BMS_CELL_MAX, sizeof(BMS_CellDataTypedef), cmp1);
	
	for (index = 0; index < BMS_CELL_MAX; index++)
	{
		printf("%f\r\n", SampleData.CellData[index].CellVoltage);
	}
	printf("\r\n");
	
    return 0;
}

*/
