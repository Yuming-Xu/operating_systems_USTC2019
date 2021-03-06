#lab3思考
##现在的问题1(已解决)
空闲链表为空要怎么表示：
（NULL）
如果搞成循环链表需要考虑将最后一个元素取出后要怎么做：
没搞循环链表
如果搞成双向非循环链表则最后一个链表下一个为NULL（这样比较好）：
且头节点设为heap_listp
代码需要大改：
不需要大改，只需要将链表安排部分改动
##现在的问题2(已解决)
无限循环
##version
###version1:
首次适配
使用的是LIFO机制
双向空闲链表：头为heap_listp，尾部为NULL
![version1](/Users/xuyuming/Downloads/os/lab3/version1.png)
####存在问题
存在几个样例空间分配不好，需要进行改动
原因：使用首次适配和LIFO使得外部碎片增多
改进思路：使用伙伴系统进行尝试
光从分数来看，助教给的测试评分很容易达到速度的4分满分，关键是空间利用率
###version1.1:
只将version1改成最佳适配
速度仍保持在4分（尽管用时已经变成了原来的15倍），但是空间利用率分数上升了0.3
也说明了如果只考虑分数来看本次实验分数中速度分数并不是瓶颈
![version1.1](/Users/xuyuming/Downloads/os/lab3/version1.1.png)
####存在问题
分配过程中仍然还会存在碎片的问题
速度变慢太多
考虑使用伙伴系统
###version2
![version2](/Users/xuyuming/Downloads/os/lab3/version2.png)
将LIFO机制改为按地址顺序维护机制，空间利用率相比version1上升了0.25，但是时间居然变成了原来的将近70倍
原因：访存开销太大，由于使用了大量的宏定义中存在过多的不必要的访存操作，比如对一个相同的数据进行了两次访存而不是储存值在临时变量中加以使用。

###version2.1
![version2.1](/Users/xuyuming/Downloads/os/lab3/version2.1.png)

在version2的基础之上将首次适配改为最佳适配，结果空间利用率不升反降，且速度又下降了一些。

##备注
各宏定义的用处：
PACK(size, alloc)：将size和alloc或运算
GET(p)：获得p中存放的值
PUT(p, val) 将val存入p所在的地址中
GET_SIZE(p)：根据p中存放的31-3位获得p空闲块的大小
特别注意：使用的时候需要将bp先跳到头部
GET_ALLOC(p): 根据p中第0位判断p块是否为空闲块
HDRP(bp):bp向前偏移4字节获得头部
FTRP(bp):获得bp的头部后根据bp块大小偏移到尾部
NEXT_BLKP(bp): 偏移到bp的头部后根据大小到下一个块
PREV_BLKP(bp): (仅适用于空闲块) 偏移到前一个块的脚部后偏移到前一个块的头部
PRED(bp): 直接就是bp的位置
SUCC(bp):bp向后偏移4字节
GET_PRED(bp)：获得bp中的值
GET_SUCC(bp): 获得bp向后偏移4字节中的地址的值
所有分配的块大小都是4字对齐的？
答：不是，是分配的时候只允许分配4的倍数

##初始化
分配一大段新的内存，然后在开头加上
```
pred=自身;
succ=自身；
```
序言块做成3字（对齐就不需要填充块了），中间第一字存第一个空闲链表的位置
##堆扩展
和csapp中的隐氏一致
需要给刚获得的空闲块附加前驱后继
##分配内存
待定：首次适配
1、从序言块的位置（已知），额外用一个临时指针开始寻找适配的内存块
2、找到内存块后
(1)若pred为自身，不记录
(2)若pred不为自身，则记录前驱和后继
3、若没有找到内存块
使用堆扩展分配新的内存块
4、使用place函数分配后 
根据剩余块大小考虑是否分割
##合并
###合并选在刚释放的时候以及堆扩展的时候
检查前一个块是否是空闲块（靠头部），检查后一个块是否是空闲块（靠头部位置+块大小）
将前后为空闲的块从空闲链表中移除
做法：将其的前驱和后继的前驱后继进行改变
case1:前后都是空闲块
将三者的头部的块长度相加，放置前空闲块头部和后空闲块尾部的31-3位，保留前一个空闲块头部的2-0位
case2:前是后不是(堆扩展的时候一定是这种情况)
将两者头部的块长度相加，放置在前空闲块头部和该空闲块尾部的31-3位，保留前一个空闲块头部的2-0位
case3：前不是后是
将两者头部的块长度相加，放置在该空闲块头部和后空闲块尾部的31-3位，保留该空闲块头部的2-0位(为010)
case4:都不是
为新块，需要插入空闲链表中，还是插入链表作为序言块指向的第一个空闲块
##释放
释放后合并