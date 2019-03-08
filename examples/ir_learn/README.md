# IR Learn Example

该示例会创建四个任务，其中三个任务分别用于发送：

- nec 协议控制命令： 
	- RMT channel: RMT_CHANNEL_0
	- gpio: GPIO_NUM_16
	- addr: 0x33
	- cmd: 0x9b

- RC5 协议控制命令：
	- RMT channel: RMT_CHANNEL_1
	- gpio: GPIO_NUM_17
	- addr: 0x14
	- cmd: 0x25

- RC6 协议控制命令：
	- RMT channel: RMT_CHANNEL_2
	- gpio: GPIO_NUM_18
	- addr: 0x34
	- cmd: 0x29

剩下一个任务用于红外学习，以及在学习成功后发送红外学习的结果；

- 学习 gpio: GPIO_NUM_19
- 发送红外学习结果 RMT channel: RMT_CHANNEL_3
- 发送红外学习结果 gpio: GPIO_NUM_21

> 关于红外学习模块的详细说明及使用，请参考模块 ir_learn (目录：components/feature/ir_learn) 的 README 文档。