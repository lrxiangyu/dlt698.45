
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <pthread.h>
#include "sys/reboot.h"
#include <wait.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "read485.h"
#include "dlt698def.h"
#include "cjdeal.h"
#include "show_ctrl.h"
#include <stdarg.h>
#include <sys/stat.h>
#include "basedef.h"
#define SAVETESTDEF
extern Proxy_Msg* p_Proxy_Msg_Data;//液晶给抄表发送代理处理结构体，指向由guictrl.c配置的全局变量
extern ProgramInfo* JProgramInfo;
extern INT8U poweroffon_state;
extern MeterPower MeterPowerInfo[POWEROFFON_NUM];

//INT8U doSave_698(INT8U* buf645,int len645);

INT8U flag07_0CF33[4] =   {0x00,0xff,0x01,0x00};//当前正向有功总电能示值
INT8U flag07_0CF34[4] =   {0x00,0xff,0x02,0x00};//当前反向有功总电能示值
INT8U flag07_0CF25_1[4] = {0x00,0xff,0x01,0x02};//当前电压
INT8U flag07_0CF25_2_O[4] = {0x01,0x00,0x80,0x02};//当前零序电流
INT8U freezeflag07_1[4] = {0x01,0x00,0x06,0x05};//上一次日冻结时标
INT8U flag07_date[4] 	= {0x01,0x01,0x00,0x04};//电能表日历时钟-日期
INT8U flag07_time[4]	= {0x02,0x01,0x00,0x04};//电能表日历时钟-时间
INT8U flag07_tingshangdian[4] = {0x01,0x00,0x11,0x03};//上一次停上电记录
INT8U flag07_diaodian[4] =  {0x01,0x00,0x11,0x03};//电能表掉电事件
INT8U flag07_qingling[4] =   {0x01,0x01,0x30,0x03};//电能清零电事件
INT8U flag07_jiaoshi[4] =   {0x01,0x04,0x30,0x03};//电能校时电事件
INT8U flag07_kaibiaogai[4] =   {0x01,0x0d,0x30,0x03};//电能表开盖事件
INT8U flag07_kaibiaogaicishu[4] =   {0x00,0x0d,0x30,0x03};//电能表开盖事件次数
INT8U flag07_diaodiancishu[4] =  {0x00,0x00,0x11,0x03};//电能表掉电次数
INT8U zeroBuff[4] = {0x00,0x00,0x00,0x00};
INT16S request9707_singleOAD(INT8U protocol,OI_698 roadOI,OAD soureOAD,CLASS_6001 to6001,CLASS_6035* st6035,INT8U* dataContent,INT8U port485);
INT16S deal6015_698(CLASS_6015 st6015, CLASS_6001 to6001,CLASS_6035* st6035,INT8U port485);
INT16U compose6012Buff(DateTimeBCD startTime,DateTimeBCD saveTime,TSA meterAddr,INT16U dataLen,INT8U* dataContent, INT8U port485);
INT8U getSaveTime(DateTimeBCD* saveTime,INT8U cjType,INT8U saveTimeFlag,DATA_TYPE curvedata);
INT16S request698_singleOAD(CLASS_6015 st6015, CLASS_6001 to6001,INT8U* data,INT8U port485);


/*
 * 去掉程序中日志的内容
 * 防止日志频繁,出现异常,DbgPrintToFile1 改为DbgPrintToFile记录,记录内容尽量少
 * */
void DbgPrintToFile1(INT8U comport,const char *format,...)
{
	if(comport == 31) {		//载波口不记录详细log
		return;
	}
#if 1
	static INT8U  log_num = 0;
	char str[50];
	char fname[100];
	char tmpcmd[256];
	time_t cur_time;
	struct tm cur_tm;
	long  int  logsize=0;
	FILE *fp = NULL;

	memset(fname,0,sizeof(fname));
	sprintf(fname,"/nand/log_698/vs485_%d.log",comport);
	memset(str,0,50);
	cur_time=time(NULL);
	localtime_r(&cur_time,&cur_tm);
	sprintf(str, "\n[%02d-%02d %02d:%02d:%02d]",
			cur_tm.tm_mon+1, cur_tm.tm_mday,cur_tm.tm_hour, cur_tm.tm_min, cur_tm.tm_sec);

	fp = fopen(fname, "a+");//内容存入文件
	if (fp != NULL)
	{
		setbuf(fp,NULL);
		va_list ap;
	    va_start(ap,format);
		vfprintf(fp,str,ap);
	    if(fp)
			vfprintf(fp,format,ap);
	    va_end(ap);
	    fflush(fp);
		fclose(fp);
	}

	struct stat fileInfo;
	if(stat(fname, &fileInfo)==-1) {
		syslog(LOG_ERR,"DbgPrintToFile stat error\n");
		sprintf(tmpcmd,"rm %s",fname);
		system(tmpcmd);
	};
	logsize = 2048*1000;	//2M	防止II型集中器log过大，nand空间不足
	//	if (fileInfo.st_size>4096*1000)//超过300K
	if (fileInfo.st_size>logsize)//超过300K
	{
		memset(tmpcmd,0,sizeof(tmpcmd));
		if(comport==31) {
			sprintf(tmpcmd,"cp %s %s.%d",fname,fname,log_num);
			log_num++;
			if(log_num>=2) {
				log_num = 0;
			}
		}else {
			sprintf(tmpcmd,"cp %s %s.0",fname,fname);
		}
		system(tmpcmd);
		sleep(3);
		memset(tmpcmd,0,sizeof(tmpcmd));
		sprintf(tmpcmd,"rm -f %s",fname);
		system(tmpcmd);
		sleep(2);
		unlink(fname);
		sync();
	}
#endif
}

void DbgPrintToFile(INT8U comport,const char *format,...)
{
	static INT8U  log_num = 0;
	char str[50];
	char fname[100];
	char tmpcmd[256];
	time_t cur_time;
	struct tm cur_tm;
	long  int  logsize=0;
	FILE *fp = NULL;

	memset(fname,0,sizeof(fname));
	sprintf(fname,"/nand/log_698/vs485_%d.log",comport);
	memset(str,0,50);
	cur_time=time(NULL);
	localtime_r(&cur_time,&cur_tm);
	sprintf(str, "\n[%02d-%02d %02d:%02d:%02d]",
			cur_tm.tm_mon+1, cur_tm.tm_mday,cur_tm.tm_hour, cur_tm.tm_min, cur_tm.tm_sec);

	fp = fopen(fname, "a+");//内容存入文件
	if (fp != NULL)
	{
		setbuf(fp,NULL);
		va_list ap;
	    va_start(ap,format);
		vfprintf(fp,str,ap);
	    if(fp)
			vfprintf(fp,format,ap);
	    va_end(ap);
	    fflush(fp);
		fclose(fp);
	}

	struct stat fileInfo;
	if(stat(fname, &fileInfo)==-1) {
		syslog(LOG_ERR,"DbgPrintToFile stat error\n");
		sprintf(tmpcmd,"rm %s",fname);
		system(tmpcmd);
	};
	logsize = 2048*1000;	//2M	防止II型集中器log过大，nand空间不足
	//	if (fileInfo.st_size>4096*1000)//超过300K
	if (fileInfo.st_size>logsize)//超过300K
	{
		memset(tmpcmd,0,sizeof(tmpcmd));
		if(comport==31) {
			sprintf(tmpcmd,"cp %s %s.%d",fname,fname,log_num);
			log_num++;
			if(log_num>=2) {
				log_num = 0;
			}
		}else {
			sprintf(tmpcmd,"cp %s %s.0",fname,fname);
		}
		system(tmpcmd);
		sleep(3);
		memset(tmpcmd,0,sizeof(tmpcmd));
		sprintf(tmpcmd,"rm -f %s",fname);
		system(tmpcmd);
		sleep(2);
		unlink(fname);
		sync();
	}
}

void DbPrt1(INT8U comport,char *prefix, char *buf, int len, char *suffix)
{
	//return ;
	char str[50], tmpbuf[2048], c[2], c1[2], c2[2];
	int i=0;
	memset(c, 0, 2);
	memset(str, 0, 50);
	memset(tmpbuf, 0, 2048);

	int count= 0;
	int prtlen=0;
	int k=0;
	while(1)
	{
		memset(c, 0, 2);
		memset(str, 0, 50);
		memset(tmpbuf, 0, 2048);
		if(len<=512)
		{
			prtlen = len;
		}else
		{
			if(k<len/512)
			{
				k++;
				prtlen = 512;
			}else
			{
				prtlen = len%512;
			}
		}
		if(prefix!=NULL)
		{
			sprintf(str, "%s[%d] ", prefix,prtlen);
			strcat(tmpbuf, str);
		}
		for(i=0; i<prtlen; i++)
		{
			memset(c, 0, 2);
			memset(c1, 0, 2);
			memset(c2, 0, 2);
			myBCDtoASC1(buf[i+count], c);

			c1[0] = c[0];
			c2[0] = c[1];
			strcat(tmpbuf, c1);
			strcat(tmpbuf, c2);
			strcat(tmpbuf, " ");
		}
		if(suffix!=NULL)
			strcat(tmpbuf, suffix);
		DbgPrintToFile(comport,tmpbuf);
		count += prtlen;
		if(count>=len)
			break;
	}
}

INT32U getMeterBaud(INT8U bps)
{
	if (bps == bps300)
		return 300;
	if (bps == bps600)
		return 600;
	if (bps == bps1200)
		return 1200;
	if (bps == bps4800)
		return 4800;
	if (bps == bps7200)
		return 7200;
	if (bps == bps9600)
		return 9600;
	if (bps == bps19200)
		return 19200;
	if (bps == bps38400)
		return 38400;
	if (bps == bps57600)
		return 57600;
	if (bps == bps115200)
		return 115200;

	return 2400;
}

/*
 * 根据测量点串口参数是否改变
 * 返回值：<=0：串口打开失败
 * 		  >0： 串口打开句柄
 * */
INT32S open_com_para_chg(INT8U port, INT32U baud, INT32S oldcomfd, unsigned char* par, unsigned char stopb, unsigned char bits) {
	INT32S newfd = 0;
	static INT8U lastport = 0;
	static INT32U lastbaud = 0;
	//fprintf(stderr,"oldcomfd = %d lastport = %d lastbaud = %d port=%d baud =%d",oldcomfd,lastport,lastbaud,port,baud);
	if ((oldcomfd>0)&&(lastbaud == baud) && (lastport == port))
	{
		return oldcomfd;
	}
	if (oldcomfd > 0) {
		CloseCom(oldcomfd);
		sleep(1);
	}
	//fprintf(stderr,"\n JProgramInfo->cfg_para.device = %d",JProgramInfo->cfg_para.device);
	if(JProgramInfo->cfg_para.device == CCTT2)
	{
		if (port==S4851)
			port = S4852;
		else if (port==S4852)
			port = S4851;
	}


	newfd = OpenCom(port, baud,par,stopb,bits);
	fprintf(stderr,"\n open_com_para_chg port = %d baud = %d newfd = %d",port,baud, newfd);
	lastport = port;
	lastbaud = baud;

	return newfd;
}
INT8S getComfdBy6001(INT8U baud,INT8U port)
{

	INT8S result = -1;
	INT32U baudrate = getMeterBaud(baud);
	//fprintf(stderr,"\n\n baud = %d port = %d baudrate = %d comfd4851 = %d comfd4852 = %d\n\n",baud,port,baudrate,comfd485[0],comfd485[1]);
	comfd485[port-1] = open_com_para_chg(port, baudrate, comfd485[port-1], (unsigned char *) "even", 1, 8);
	if (comfd485[port-1] <= 0)
	{
		fprintf(stderr, "打开S485%d串口失败\n",port);
		return result;
	}

	return 1;
}
/*
 * 根据6013任务配置单元去文件里查找对应的6015普通采集方案
 * 事件采集方案6017 放到6015里面
 * 输入 cjType 采集方案类型；方案ID fanganID
 * 输出 st6015
 */
INT8S use6013find6015or6017(INT8U cjType,INT16U fanganID,TI interval6013,CLASS_6015* st6015)
{
	INT8S result = -1;
	if(cjType == norm)
	{
		OI_698 oi = 0x6015;
		if (readCoverClass(oi, fanganID, st6015, sizeof(CLASS_6015), coll_para_save)== 1)
		{
#if 1
			//调整6015采集方案　过滤掉不需要抄表的内容
			if((st6015->cjtype == TYPE_LAST)||(st6015->cjtype == TYPE_FREEZE))
			{
				MY_CSD destCSDS[MY_CSD_NUM];//CSD
				memset(destCSDS,0,sizeof(MY_CSD)*MY_CSD_NUM);
				INT8U csdIndex = 0;
				INT8U csdCount = 0;
				for(csdIndex = 0;csdIndex < st6015->csds.num;csdIndex++)
				{
					if(st6015->csds.csd[csdIndex].type == 1)
					{
						memcpy(&destCSDS[csdCount],&st6015->csds.csd[csdIndex],sizeof(MY_CSD));
						csdCount++;
					}
				}
				st6015->csds.num = csdCount;
				memcpy(st6015->csds.csd,destCSDS,sizeof(MY_CSD)*MY_CSD_NUM);
			}
#endif
			if(st6015->cjtype == TYPE_INTERVAL)
			{
				//计算需要抄多少个数据点
				INT16U minSpan = 0;//任务执行间隔-分钟
				INT16U minInterVal = 0;//冻结时标间隔-分钟
				INT16U dataNum = 0;//需要抄读冻结数据点数
				if(interval6013.units < st6015->data.data[0])
				{
					asyslog(LOG_NOTICE,"参数错误,执行频率小于冻结间隔");
				}
				switch (interval6013.units)
				{
					case day_units:
						minSpan = interval6013.interval*60*24;
						break;
					case hour_units:
						minSpan = interval6013.interval*60;
						break;
					default:
						minSpan = interval6013.interval;
				}

				if(st6015->data.data[0] == minute_units)
				{
					minInterVal = (st6015->data.data[1]<<8)+st6015->data.data[2];
				}
				if(st6015->data.data[0] == hour_units)
				{
					minInterVal = ((st6015->data.data[1]<<8)+st6015->data.data[2])*60;
				}

				dataNum = minSpan/minInterVal;
				fprintf(stderr,"\n\n 曲线任务执行频率分=%d　 冻结时标间隔=%d分　　数据点数=%d \n\n",minSpan,minInterVal,dataNum);
				//计算开始时标
				TS ts_start;
				TSGet(&ts_start);

				if(interval6013.units == minute_units)
				{
					INT16U minInterval = 0;
					minInterval = interval6013.interval;
					INT8U minIntervalindex;
					for(minIntervalindex=0;minIntervalindex<minInterval;minIntervalindex++)
					{
						if((ts_start.Minute%minInterval) == 0)
						{
							break;
						}
						else
						{
							tminc(&ts_start,minute_units,-1);
						}

					}
				}
				else
				{
					INT16U hourInterVal = 0;
					if(interval6013.units == day_units)
					{
						hourInterVal = interval6013.interval*24;
					}
					if(interval6013.units == hour_units)
					{
						hourInterVal = interval6013.interval;
					}
					INT8U hourindex;
					for(hourindex=0;hourindex<hourInterVal;hourindex++)
					{
						if((ts_start.Hour%hourInterVal) == 0)
						{
							break;
						}
						else
						{
							tminc(&ts_start,hour_units,-1);
						}
					}
				}

				INT16U tmpTime = ts_start.Year;
				st6015->data.data[CURVE_INFO_STARTINDEX+16] = (tmpTime>>8)&0x00ff;
				st6015->data.data[CURVE_INFO_STARTINDEX+17] = tmpTime&0x00ff;
				st6015->data.data[CURVE_INFO_STARTINDEX+18] = ts_start.Month;
				st6015->data.data[CURVE_INFO_STARTINDEX+19] = ts_start.Day;
				st6015->data.data[CURVE_INFO_STARTINDEX+20] = ts_start.Hour;
				if(interval6013.units == minute_units)
				{
					st6015->data.data[CURVE_INFO_STARTINDEX+21] = ts_start.Minute;
				}
				else
				{
					st6015->data.data[CURVE_INFO_STARTINDEX+21] = 0;
				}

				st6015->data.data[CURVE_INFO_STARTINDEX+22] = (dataNum>>8)&0x00ff;
				st6015->data.data[CURVE_INFO_STARTINDEX+23] = dataNum&0x00ff;


				INT8U csdIndex = 0;
				for(csdIndex = 0;csdIndex < st6015->csds.num;csdIndex++)
				{
					if(st6015->csds.csd[csdIndex].type == 1)
					{
						INT8U ishas2021 = 0;
						ROAD sourceRoad;
						memcpy(&sourceRoad,&st6015->csds.csd[csdIndex].csd.road,sizeof(ROAD));

						st6015->csds.csd[csdIndex].csd.road.oads[0].OI = DATA_TIMESTAMP_OI;
						st6015->csds.csd[csdIndex].csd.road.oads[0].attflg = 0x02;
						st6015->csds.csd[csdIndex].csd.road.oads[0].attrindex = 0x00;
						st6015->csds.csd[csdIndex].csd.road.num = 1;
						INT8U oadIndex = 0;
						for(oadIndex = 0;oadIndex < sourceRoad.num;oadIndex++)
						{
							if(sourceRoad.oads[oadIndex].OI == DATA_TIMESTAMP_OI)
							{
								ishas2021 =1;
								continue;
							}
							else
							{
								st6015->csds.csd[csdIndex].csd.road.oads[st6015->csds.csd[csdIndex].csd.road.num].OI = sourceRoad.oads[oadIndex].OI;
								st6015->csds.csd[csdIndex].csd.road.oads[st6015->csds.csd[csdIndex].csd.road.num].attflg = sourceRoad.oads[oadIndex].attflg;
								st6015->csds.csd[csdIndex].csd.road.oads[st6015->csds.csd[csdIndex].csd.road.num].attrindex = sourceRoad.oads[oadIndex].attrindex;
								st6015->csds.csd[csdIndex].csd.road.num++;
							}
						}
#if 0
						if(ishas2021 == 0)
						{
							memcpy(&st6015->csds.csd[csdIndex].csd.road,&sourceRoad,sizeof(ROAD));
						}
#endif
					}
				}


			}
			return 1;
		}
	}
	if(cjType == events)
	{
		OI_698 oi = 0x6017;
		CLASS_6017 st6017;
		memset(&st6017,0,sizeof(CLASS_6017));
		if (readCoverClass(oi, fanganID, &st6017, sizeof(CLASS_6017), coll_para_save)== 1)
		{
			//把6017内容填充到6015里面

			memset(st6015,0,sizeof(CLASS_6015));
			st6015->cjtype = TYPE_LAST;
			st6015->csds.num = st6017.collstyle.roads.num;
			st6015->deepsize = st6017.deepsize;
			st6015->sernum = st6017.sernum;
			st6015->savetimeflag = st6017.ifreport;

			memcpy(&st6015->mst,&st6017.ms,sizeof(MY_MS));
			INT8U csdIndex = 0;

			for(csdIndex = 0;csdIndex < st6017.collstyle.roads.num;csdIndex++)
			{
				st6015->csds.csd[csdIndex].type = 1;
				st6015->csds.csd[csdIndex].csd.road.oad.OI = st6017.collstyle.roads.road[csdIndex].oad.OI;
				st6015->csds.csd[csdIndex].csd.road.oad.attflg = st6017.collstyle.roads.road[csdIndex].oad.attflg;
				st6015->csds.csd[csdIndex].csd.road.oad.attrindex= st6017.collstyle.roads.road[csdIndex].oad.attrindex;

				//事件序号
				st6015->csds.csd[csdIndex].csd.road.oads[0].OI = EVENT_INDEX_OI;
				st6015->csds.csd[csdIndex].csd.road.oads[0].attflg = 0x02;
				st6015->csds.csd[csdIndex].csd.road.oads[0].attrindex = 0x00;
				//事件发生时间
				st6015->csds.csd[csdIndex].csd.road.oads[1].OI = EVENTSTART_TIME_OI;
				st6015->csds.csd[csdIndex].csd.road.oads[1].attflg = 0x02;
				st6015->csds.csd[csdIndex].csd.road.oads[1].attrindex = 0x00;
				//事件结束时间
				st6015->csds.csd[csdIndex].csd.road.oads[2].OI = EVENTSTART_END_OI;
				st6015->csds.csd[csdIndex].csd.road.oads[2].attflg = 0x02;
				st6015->csds.csd[csdIndex].csd.road.oads[2].attrindex = 0x00;
				st6015->csds.csd[csdIndex].csd.road.num = 3;
				INT8U oadIndex = 0;
				for(oadIndex = 0;oadIndex < st6017.collstyle.roads.road[csdIndex].num;oadIndex++)
				{
					if((st6017.collstyle.roads.road[csdIndex].oads[oadIndex].OI == EVENT_INDEX_OI)
						||(st6017.collstyle.roads.road[csdIndex].oads[oadIndex].OI == EVENTSTART_TIME_OI)
						||(st6017.collstyle.roads.road[csdIndex].oads[oadIndex].OI == EVENTSTART_END_OI))
					{
						continue;
					}
					else
					{
						st6015->csds.csd[csdIndex].csd.road.oads[st6015->csds.csd[csdIndex].csd.road.num].OI = st6017.collstyle.roads.road[csdIndex].oads[oadIndex].OI;
						st6015->csds.csd[csdIndex].csd.road.oads[st6015->csds.csd[csdIndex].csd.road.num].attflg = st6017.collstyle.roads.road[csdIndex].oads[oadIndex].attflg;
						st6015->csds.csd[csdIndex].csd.road.oads[st6015->csds.csd[csdIndex].csd.road.num].attrindex = st6017.collstyle.roads.road[csdIndex].oads[oadIndex].attrindex;
						st6015->csds.csd[csdIndex].csd.road.num++;
					}
				}
			}


			return 1;
		}
	}

	return result;

}

void printbuff(const char* prefix, INT8U* buff, INT32U len, const char* format,
		const char* space, const char* surfix) {
	if (prefix != NULL )
		fprintf(stderr, "%s", prefix);
	if (buff != NULL && len > 0) {
		INT32U i = 0;
		for (i = 0; i < len; i++) {
			fprintf(stderr, (const char*) format, *(buff + i));
			if (space != NULL )
				fprintf(stderr, "%s", space);
		}
	}
	if (surfix != NULL )
		fprintf(stderr, "%s", surfix);
}
/*
 * 485口接收处理,判断完整帧
 * 输入参数：delayms，超时等待时间，单位：毫秒
 * 输出；*str：接收缓冲区
 * 返回：>0：完整报文；=0:接收长度为0；-1：乱码，无完整报文
 */
INT16S ReceDataFrom485(METER_PROTOCOL meterPro,INT8U port485, INT16U delayms, INT8U *str) {
	INT8U TmprevBuf[BUFFSIZE2048];	//接收报文临时缓冲区
	INT8U prtstr[50];
	INT16U len_Total = 0, len, rec_step, rec_head, rec_tail, DataLen, i, j;
	INT32S fd = comfd485[port485-1];
	//char title[20];

	if (fd <= 2)
		return -1;

	memset(TmprevBuf, 0, BUFFSIZE2048);
	rec_head = rec_tail = rec_step = DataLen = 0;
	//fprintf(stderr, "\n ReceDataFrom485 delayms=%d\n", delayms);
	//zhejiangtest
#if 0
	usleep(delayms * 2000);
#else
	usleep(delayms * 1000);
#endif
	for (j = 0; j < 15; j++) {
	#if 0
		usleep(200000);
	#else
		usleep(20000);
	#endif

		len = read(fd, TmprevBuf, BUFFSIZE2048);

		if (len > 0) {
			len_Total += len;
			if (len_Total >= BUFFSIZE2048) {
				fprintf(stderr, "len_Total=%d, xxxxxxxxxxx\n", len_Total);
				return -1;
			}
			for (i = 0; i < len; i++) {
				str[rec_head++] = TmprevBuf[i];
			}

			memset(prtstr, 0, sizeof(prtstr));
			sprintf((char *) prtstr, "485(%d)_R(%d):", port485, len);
			printbuff((char *) prtstr, TmprevBuf, len, "%02x", " ", "\n");

		}
		switch (rec_step) {
		case 0:
			if (rec_tail < rec_head) {
				for (i = rec_tail; i < rec_head; i++) {
					if (str[i] == 0x68) {	//ma:判断第一个字符是否为0x68
						rec_step = 1;
						rec_tail = i;
						break;
					} else
						rec_tail++;
				}
			}
			break;
		case 1:
			{
				if(meterPro == DLT_645_07)
				{
					if ((rec_head - rec_tail) >= 10)
					{
						if (str[rec_tail] == 0x68 && str[rec_tail + 7] == 0x68)
						{
							DataLen = str[rec_tail + 9];	//获取报文数据块长度
							rec_step = 2;
							break;
						} else
							rec_tail++;
					}
				}
				if(meterPro == DLT_698)
				{
					if ((rec_head - rec_tail) >= 3)
					{
							if (str[rec_tail] == 0x68)
							{
								DataLen = (str[rec_tail + 2]<<8);
								DataLen += str[rec_tail + 1];	//获取报文数据块长度
								rec_step = 2;
								break;
							} else
								rec_tail++;
					}
				}
			}


			break;
		case 2:
			{
				if(meterPro == DLT_645_07)
				{
					if ((rec_head - rec_tail) >= (DataLen + 2))
					{
						if (str[rec_tail + 9 + DataLen + 2] == 0x16) {
							DbPrt1(port485,"R:",(char *)str, rec_head, NULL);

							if((getZone("GW")==0) && (port485==1)) {
								PacketBufToFile(0,"[485_07]R:",(char *)str, rec_head, NULL);
							}
							return (rec_tail + 9 + DataLen + 3);
						}
					}
				}
				if(meterPro == DLT_698)
				{
					//fprintf(stderr,"\n\n rec_head = %d rec_tail = %d DataLen = %d\n\n",rec_head,rec_tail,DataLen);
					//fprintf(stderr,"str[rec_tail + DataLen +1] = %02x",str[rec_tail + DataLen +1]);
					if ((rec_head - rec_tail) >= (DataLen + 1))
					{
							if (str[rec_tail + DataLen +1] == 0x16) {
								DbPrt1(port485,"R:",(char *)str, rec_head, NULL);
								if((getZone("GW")==0) && (port485==1)) {
									PacketBufToFile(0,"[485_698]R:",(char *)str, rec_head, NULL);
								}
								return rec_head;
							}
					}
				}
			}
			break;
		default:
			break;
		}
	}
	if (len_Total > 0)
		return -1;
	else
		return 0;
}

/**
 * 485口发送
 */
void SendDataTo485(INT8U port485, INT8U *sendbuf, INT16U sendlen) {
	INT32S fd = comfd485[port485-1];

	ssize_t slen;

	INT8U str[50];
	memset(str, 0, 50);

#if 1
	sprintf((char *) str, "485(%d)_S(%d):", port485, sendlen);
	printbuff((char *) str, sendbuf, sendlen, "%02x", " ", "\n");

	fprintf(stderr,"\n port485 = %d fd = %d \n",port485,fd);
#endif
	slen = write(fd, sendbuf, sendlen);
	if (slen < 0)
		fprintf(stderr, "slen=%d,send err!\n", slen);
	DbPrt1(port485,"S:", (char *) sendbuf, sendlen, NULL);
	if((getZone("GW")==0) && (port485==1)) {
		PacketBufToFile(0,"[485]S:",(char *) sendbuf, sendlen, NULL);
	}
}

//07数据YYMMDDHHMMSS 转换为698时间
INT8U time07totime698(INT8U* time07,INT8U* time698)
{
	time698[0] = dtdatetimes;

	INT16U year  = (time07[5] >> 4)*10 + (time07[5]&0x0f) + 2000;
	INT8U month = (time07[4] >> 4)*10 + (time07[4]&0x0f);
	INT8U day = (time07[3] >> 4)*10 + (time07[3]&0x0f);
	INT8U hour  = (time07[2] >> 4)*10 + (time07[2]&0x0f);
	INT8U minute = (time07[1] >> 4)*10 + (time07[1]&0x0f);
	INT8U second = (time07[0] >> 4)*10 + (time07[0]&0x0f);

	time698[1] = (year>>8)&0x00ff;
	time698[2] = year&0x00ff;
	time698[3] = month;
	time698[4] = day;
	time698[5] = hour;
	time698[6] = minute;
	time698[7] = second;

	return 8;
}
INT8U checkEvent(CLASS_6001 meter,FORMAT07 resultData07,INT16U taskID)
{
	INT8U ret = 0;
	if(memcmp(flag07_0CF33,resultData07.DI,4)==0)
	{
		fprintf(stderr,"\n\n&&&&&&&&&&&checkEvent taskID = %d  mter.sernum = %d\n\n",taskID,meter.sernum);
		ret = Event_310B(meter.basicinfo.addr,taskID,resultData07.Data,resultData07.Length,JProgramInfo);

		ret = Event_310C(meter.basicinfo.addr,taskID,resultData07.Data,resultData07.Length,JProgramInfo,meter);

		ret = Event_310D(meter.basicinfo.addr,taskID,resultData07.Data,resultData07.Length,JProgramInfo,meter);

		ret = Event_310E(meter.basicinfo.addr,taskID,resultData07.Data,resultData07.Length,JProgramInfo);
	}
	if((memcmp(flag07_date,resultData07.DI,4)==0)||(memcmp(flag07_time,resultData07.DI,4)==0))
	{
		ret = Event_3105(meter.basicinfo.addr,taskID,resultData07.Data,resultData07.Length,JProgramInfo);
	}
	return ret;
}

//TODO: 0030,0040：正向无功，反向无功的处理
INT8U  checkSumEnergy_07(CLASS_6001 meter,INT16U taskID,INT8U *DI,INT8U len,INT8U *data)
{
	INT8U ret = 0;

	fprintf(stderr,"DI=%02x_%02x_%02x_%02x\n",DI[0],DI[1],DI[2],DI[3]);
	if((JProgramInfo->cfg_para.device == SPTF3)&&((memcmp(flag07_0CF33,DI,4)==0)||(memcmp(flag07_0CF34,DI,4)==0)))
	{
		//更新总加组电量
		OAD oadCur = {0};
		OAD readoad = {0};
		if(memcmp(flag07_0CF33,DI,4)==0) {
			readoad.OI = 0x0010;
			readoad.attflg = 02;
			readoad.attrindex = 0;
		}else if(memcmp(flag07_0CF34,DI,4)==0) {
			readoad.OI = 0x0020;
			readoad.attflg = 02;
			readoad.attrindex = 0;
		}
		fprintf(stderr,"resultData07.Length = %d\n",len);
		for(int i=0;i<len;i++) {
			fprintf(stderr,"%02x ",data[i]);
		}
		ret = fillclass23data(oadCur,readoad,meter.basicinfo.addr,data,JProgramInfo);
	}
	return ret;
}

INT16S dealEventRecord(CLASS_6001 meter,FORMAT07 resultData07,INT16U taskID,INT8U* dataContent)
{

	INT16U dataLen = 0;

	if(resultData07.DI[3] == 0x03)
	{
		//电能表掉电次数//电能表开盖事件次数
		if((memcmp(flag07_diaodiancishu,resultData07.DI,4)==0)
			||(memcmp(flag07_kaibiaogaicishu,resultData07.DI,4)==0))
		{
			dataContent[dataLen++] = dtdoublelongunsigned;
			dataContent[dataLen++] = 0;
			memcpy(&dataContent[dataLen],&resultData07.Data[0],3);
			dataLen +=3;
		}
		if(memcmp(flag07_diaodian,resultData07.DI,4)==0)//电能表掉电事件
		{
			dataLen += time07totime698(&resultData07.Data[0],&dataContent[dataLen]);
			dataLen += time07totime698(&resultData07.Data[6],&dataContent[dataLen]);
		}
		if(memcmp(flag07_qingling,resultData07.DI,4)==0)//电能清零电事件
		{
			fprintf(stderr,"\n checkEvent 电能清零电事件");
			dataLen += time07totime698(&resultData07.Data[0],&dataContent[dataLen]);
			dataLen += time07totime698(&resultData07.Data[0],&dataContent[dataLen]);
		}
		if(memcmp(flag07_jiaoshi,resultData07.DI,4)==0)//电能校时电事件
		{
			fprintf(stderr,"\n checkEvent 电能校时电事件");
			dataLen += time07totime698(&resultData07.Data[4],&dataContent[dataLen]);
			dataLen += time07totime698(&resultData07.Data[10],&dataContent[dataLen]);
		}
		if(memcmp(flag07_kaibiaogai,resultData07.DI,4)==0)//电能表开盖事件
		{
			fprintf(stderr,"\n checkEvent 电能表开盖事件");
			INT8U prtIndex;
			for(prtIndex = 0;prtIndex < 60;prtIndex++)
			{
				fprintf(stderr," %02x",resultData07.Data[prtIndex]);
			}
			dataLen += time07totime698(&resultData07.Data[0],&dataContent[dataLen]);
			dataLen += time07totime698(&resultData07.Data[6],&dataContent[dataLen]);

			INT8U dataIndex;
			for(dataIndex = 0;dataIndex < 12;dataIndex++)
			{
				INT8U index07 = 12 + dataIndex*4;
				INT8U invalidBuff[4] ={0xff,0xff,0xff,0xff};
				if(memcmp(invalidBuff,&resultData07.Data[index07],4)==0)
				{
					dataContent[dataLen++] = 0;
				}
				else
				{
					dataContent[dataLen++] = dtdoublelongunsigned;
					//目前07单个数据单元最大字节数为4
					INT32U value = 0;
					INT8U reverBuff[4] = {0};
					bcd2int32u(&resultData07.Data[index07],4,inverted,&value);
					memcpy(&resultData07.Data[index07],&value,4);
					reversebuff(&resultData07.Data[index07], 4, reverBuff);
					memcpy(&dataContent[dataLen],reverBuff,4);

					dataLen += 4;
				}

			}

		}
		if(memcmp(flag07_tingshangdian,resultData07.DI,4)==0)//停上电
		{
			fprintf(stderr,"\n checkEvent 停上电事件");
			dataLen += time07totime698(&resultData07.Data[0],&dataContent[dataLen]);
			dataLen += time07totime698(&resultData07.Data[0],&dataContent[dataLen]);
		}
	}
	return dataLen;
}

//根据07 DI 返回数据类型dataType 数组大小size 信息
INT8U getASNInfo(FORMAT07* DI07,Base_DataType* dataType)
{
	INT8U flagnegative[10] = {0};
	fprintf(stderr, "\n getASNInfo DI07 = %02x%02x%02x%02x",DI07->DI[3],DI07->DI[2],DI07->DI[1],DI07->DI[0]);
	fprintf(stderr,"\n数据　%02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			DI07->Data[0],DI07->Data[1],DI07->Data[2],
			DI07->Data[3],DI07->Data[4],DI07->Data[5],DI07->Data[6],DI07->Data[7],DI07->Data[8]);
	INT8U unitNum = 1;
	INT8U index;

	//电表日期
	if(memcmp(flag07_date,DI07->DI,4) == 0)
	{
		*dataType = dtdatetimes;
		unitNum = 1;
		INT16U year  = (DI07->Data[3] >> 4)*10 + (DI07->Data[3]&0x0f) + 2000;
		INT8U month = (DI07->Data[2] >> 4)*10 + (DI07->Data[2]&0x0f);
		INT8U day = (DI07->Data[1] >> 4)*10 + (DI07->Data[1]&0x0f);
		DI07->Data[0] = (year>>8)&0x00ff;
		DI07->Data[1] = year&0x00ff;
		DI07->Data[2] = month;
		DI07->Data[3] = day;
		return unitNum;
	}

	//电表时间
	if(memcmp(flag07_time,DI07->DI,4) == 0)
	{
		*dataType = dtnull;
		unitNum = 1;
		INT8U hour  = (DI07->Data[2] >> 4)*10 + (DI07->Data[2]&0x0f);
		INT8U minute = (DI07->Data[1] >> 4)*10 + (DI07->Data[1]&0x0f);
		INT8U second = (DI07->Data[0] >> 4)*10 + (DI07->Data[0]&0x0f);
		DI07->Data[0] = hour;
		DI07->Data[1] = minute;
		DI07->Data[2] = second;
		return unitNum;
	}

	for (index = 0; index < map07DI_698OAD_NUM; index++)
	{
		if((map07DI_698OAD[index].flag07.DI_1[0][3]==DI07->DI[3])
				&&(map07DI_698OAD[index].flag07.DI_1[0][2]==DI07->DI[2]))
			{
				//实时数据考虑的有分费率分相匹配前两个就可以
				if(DI07->DI[3]!=0x05)
				{
					*dataType = map07DI_698OAD[index].datatype;
					if(DI07->DI[1]==0xff)
					{
						unitNum = map07DI_698OAD[index].unitnum;
					}
					else
					{
						unitNum = 1;
					}
					break;
				}
				else
				{
					if((map07DI_698OAD[index].flag07.DI_1[0][1]==DI07->DI[1])
									&&(map07DI_698OAD[index].flag07.DI_1[0][0]==DI07->DI[0]))
					{
						*dataType = map07DI_698OAD[index].datatype;
						unitNum = map07DI_698OAD[index].unitnum;
						break;
					}
				}

			}
	}
	//电压　电流 功率 特殊处理  07回来的是3个字节  6984个字节
	if(memcmp(flag07_0CF25_1,DI07->DI,4) == 0)
	{

		if((DI07->Data[2] == 0xff)&&(DI07->Data[3] == 0xff))
		{
			memset(&DI07->Data[2],0,2);
		}
		if((DI07->Data[4] == 0xff)&&(DI07->Data[5] == 0xff))
		{
			memset(&DI07->Data[4],0,2);
		}
	}
	//功率因数
	if((DI07->DI[2]==0x06)&&(DI07->DI[3]==0x02))
	{
		INT8U tmpIndex = 0;
		for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
		{
			if((DI07->Data[tmpIndex*2] == 0xff)&&(DI07->Data[tmpIndex*2+1] == 0xff))
			{
				memset(&DI07->Data[2],0,2);
			}
			else
			{
				if((DI07->Data[tmpIndex*2+1]&0x80) == 0x80)
				{
					DI07->Data[tmpIndex*2+1] = DI07->Data[tmpIndex*2+1]&0x7f;
					flagnegative[tmpIndex] = 1;
				}
			}
		}
	}
	if(((DI07->DI[2] == 0x02)&&(DI07->DI[3] == 0x02))
		||(memcmp(flag07_0CF25_2_O,DI07->DI,4) == 0))
	{
		*dataType = dtdoublelong;
		INT8U f25_2_buff[16] = {0};

		if(memcmp(flag07_0CF25_2_O,DI07->DI,4) == 0)
		{
			unitNum = 1;
		}
		INT8U tmpIndex = 0;
		for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
		{
			if((DI07->Data[tmpIndex*3]==0xff)&&(DI07->Data[tmpIndex*3+1]==0xff)&&(DI07->Data[tmpIndex*3+2]==0xff))
			{
				memset(&f25_2_buff[tmpIndex*4],0,4);
			}
			else
			{
				if((DI07->Data[tmpIndex*3+2]&0x80) == 0x80)
				{
					DI07->Data[tmpIndex*3+2] = DI07->Data[tmpIndex*3+2]&0x7f;
					flagnegative[tmpIndex] = 1;
				}
				memcpy(&f25_2_buff[(tmpIndex*4)],&DI07->Data[tmpIndex*3],3);
			//	fprintf(stderr,"\n tmpIndex = %d DI07->Data =  %02x%02x%02x f25_2_buff = %02x%02x%02x%02x\n ",tmpIndex,
			//			DI07->Data[0],DI07->Data[1],DI07->Data[2],f25_2_buff[0],f25_2_buff[1],f25_2_buff[2],f25_2_buff[3]);
			}
		}
		memcpy(&DI07->Data[0],f25_2_buff,16);
		//fprintf(stderr,"\n DI07->Data = %02x%02x%02x%02x\n ",DI07->Data[0],DI07->Data[1],DI07->Data[2],DI07->Data[3]);
		if(unitNum == 1)
		{
			DI07->Length += 1;
		}
		else
		{
			DI07->Length += 7;
		}
	}
	//电表状态字
	if((DI07->DI[3] == 0x04)&&(DI07->DI[2] == 0x00)&&(DI07->DI[1]==0x05))
	{
		*dataType = dtnull;
		if(DI07->DI[0]==0xff)
		{
			unitNum = 7;
		}
		else
		{
			unitNum = 1;
		}
		DI07->Length = 4;
		INT8U zhuantaizi[28];
		memset(zhuantaizi,0,28);
		INT8U tmpIndex = 0;
		for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
		{
			zhuantaizi[tmpIndex*4] = dtbitstring;
			zhuantaizi[tmpIndex*4+1] = 0x10;
			//最大需量
			if((DI07->Data[tmpIndex*2]==0xff)&&(DI07->Data[tmpIndex*2+1]==0xff))
			{
				memset(&zhuantaizi[tmpIndex*4+2],0,2);
			}
			else
			{
				zhuantaizi[tmpIndex*4+2] = DI07->Data[tmpIndex*2+1];
				zhuantaizi[tmpIndex*4+3] = DI07->Data[tmpIndex*2];
			}
			DI07->Length += 4;

		}
		memcpy(DI07->Data,zhuantaizi,28);
		return unitNum;
	}
	//需量类

	if(((DI07->DI[3] == 0x01)&&(DI07->DI[2] >= 0x00)&&(DI07->DI[2]<=0x0A))
		||((DI07->DI[3] == 0x05)&&(DI07->DI[2] == 0x06)&&(DI07->DI[1]==0x09))
		||((DI07->DI[3] == 0x05)&&(DI07->DI[2] == 0x06)&&(DI07->DI[1]==0x0A)))
	{
		*dataType = dtnull;
		//02 02 06 xx xx xx xx 1c xx xx xx xx xx xx xx
		DI07->Length = 4;
		INT8U xuliangdata[75];
		memset(xuliangdata,0,75);

		if((DI07->DI[1]==0xff)||(DI07->DI[3] == 0x05))
		{
			unitNum = 5;
		}
		else
		{
			unitNum = 1;
		}
		INT8U tmpIndex = 0;
		for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
		{
			xuliangdata[tmpIndex*15] = dtstructure;
			xuliangdata[tmpIndex*15+1] = 0x02;
			xuliangdata[tmpIndex*15+2] = dtdoublelongunsigned;
			//最大需量
			if((DI07->Data[tmpIndex*8]==0xff)&&(DI07->Data[tmpIndex*8+1]==0xff)&&(DI07->Data[tmpIndex*8+2]==0xff))
			{
				memset(&xuliangdata[tmpIndex*15+3],0,4);
			}
			else
			{
				INT8U tmpBuff[4] = {0};
				INT8U reverBuff[4] = {0};
				if((DI07->Data[tmpIndex*8+2]&0x80) == 0x80)
				{
					DI07->Data[tmpIndex*8+2] = DI07->Data[tmpIndex*8+2]&0x7f;
					flagnegative[tmpIndex] = 1;
				}
				memcpy(&tmpBuff[0],&DI07->Data[tmpIndex*8],3);
				INT32U value = 0;
				bcd2int32u(tmpBuff,4,inverted,&value);
				fprintf(stderr,"\n value = %d",value);
				memcpy(tmpBuff,&value,4);

				//负数值处理
				if(flagnegative[tmpIndex]==1)
				{
					INT32S svalue = value*(-1);
					memcpy(tmpBuff,&svalue,4);
				}

				reversebuff(tmpBuff, 4, reverBuff);
				memcpy(&xuliangdata[tmpIndex*15+3],reverBuff,4);
			}
			//发生时间
			xuliangdata[tmpIndex*15+7] = dtdatetimes;

			INT16U year  = (DI07->Data[tmpIndex*8+7] >> 4)*10 + (DI07->Data[tmpIndex*8+7]&0x0f) + 2000;
			INT8U month = (DI07->Data[tmpIndex*8+6] >> 4)*10 + (DI07->Data[tmpIndex*8+6]&0x0f);
			INT8U day = (DI07->Data[tmpIndex*8+5] >> 4)*10 + (DI07->Data[tmpIndex*8+5]&0x0f);
			INT8U hour  = (DI07->Data[tmpIndex*8+4] >> 4)*10 + (DI07->Data[tmpIndex*8+4]&0x0f);
			INT8U minute = (DI07->Data[tmpIndex*8+3] >> 4)*10 + (DI07->Data[tmpIndex*8+3]&0x0f);

			xuliangdata[tmpIndex*15+8] = (year>>8)&0x00ff;
			xuliangdata[tmpIndex*15+9] =  year&0x00ff;
			xuliangdata[tmpIndex*15+10] = month;
			xuliangdata[tmpIndex*15+11] = day;
			xuliangdata[tmpIndex*15+12] = hour;
			xuliangdata[tmpIndex*15+13] = minute;
			xuliangdata[tmpIndex*15+14] = 0;
			DI07->Length += 15;
		}
		memcpy(&DI07->Data[0],xuliangdata,unitNum*15);
		fprintf(stderr,"需量 DI07->Length = %d",DI07->Length);
		return unitNum;
	}
	//功率
	if((DI07->DI[3]==0x02)&&((DI07->DI[2]==0x03)||(DI07->DI[2]==0x04)||(DI07->DI[2]==0x05)))
	{
		*dataType = dtdoublelong;
		INT8U f25_3_buff[16] = {0};
		INT8U tmpIndex = 0;
		for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
		{
			if((DI07->Data[tmpIndex*3]==0xff)&&(DI07->Data[tmpIndex*3+1]==0xff)&&(DI07->Data[tmpIndex*3+2]==0xff))
			{
				memset(&f25_3_buff[tmpIndex*4],0,4);
			}
			else
			{
				if((DI07->Data[tmpIndex*3+2]&0x80) == 0x80)
				{
					DI07->Data[tmpIndex*3+2] = DI07->Data[tmpIndex*3+2]&0x7f;
					flagnegative[tmpIndex] = 1;
				}
				memcpy(&f25_3_buff[tmpIndex*4],&DI07->Data[tmpIndex*3],3);
			}
		}
		memcpy(DI07->Data,f25_3_buff,16);
		DI07->Length += unitNum;
	}
	//冻结时标数据07为5个字节 698为7个 需要特殊处理
	if(memcmp(freezeflag07_1,DI07->DI,4) == 0)
	{
		INT16U year  = (DI07->Data[4] >> 4)*10 + (DI07->Data[4]&0x0f) + 2000;
		INT8U month = (DI07->Data[3] >> 4)*10 + (DI07->Data[3]&0x0f);
		INT8U day = (DI07->Data[2] >> 4)*10 + (DI07->Data[2]&0x0f);
		memset(DI07->Data,0,7);
		DI07->Data[0] = (year>>8)&0x00ff;
		DI07->Data[1] = year&0x00ff;
		DI07->Data[2] = month;
		DI07->Data[3] = day;
		*dataType = dtdatetimes;
		DI07->Length += 2;
		fprintf(stderr,"\n 1-----getASNInfo dataType = %d",*dataType);
	}
	else//698数据格式和07数据有区别
	{
		INT8U unitIndex;
		INT8U unitSize = (DI07->Length-4)/unitNum;
		fprintf(stderr,"DI07->Length = %d unitNum = %d unitSize = %d",DI07->Length,unitNum,unitSize);
		for(unitIndex = 0;unitIndex < unitNum; unitIndex++)
		{
			//目前07单个数据单元最大字节数为4
			INT8U reverBuff[4] = {0};
			INT32U value = 0;
			INT8U dataIndex = unitIndex*unitSize;
			bcd2int32u(&DI07->Data[dataIndex],unitSize,inverted,&value);
			fprintf(stderr,"\n value = %d",value);
			memcpy(&DI07->Data[dataIndex],&value,unitSize);
			//负数值处理
			if(flagnegative[unitIndex]==1)
			{
				INT32S svalue = value*(-1);
				memcpy(&DI07->Data[dataIndex],&svalue,unitSize);
			}
			if(unitSize == 2)
			{
				fprintf(stderr,"\n 123123  DI07->Data[%d] = %d  DI07->Data[%d+1] = %d",dataIndex,dataIndex,DI07->Data[dataIndex],DI07->Data[dataIndex+1]);
				INT8U tmpvalue = DI07->Data[dataIndex];
				DI07->Data[dataIndex] = DI07->Data[dataIndex+1];
				DI07->Data[dataIndex+1] = tmpvalue;
			}
			else
			{
				reversebuff(&DI07->Data[dataIndex], 4, reverBuff);
				memcpy(&DI07->Data[dataIndex],reverBuff,unitSize);
			}

		}

	}


	return unitNum;
}
//根据07 DI 返回数据类型dataType 数组大小size 信息
INT8U getASNInfo97(FORMAT97* DI97,Base_DataType* dataType)
{
	fprintf(stderr, "\n getASNInfo DI97 = %02x%02x",DI97->DI[1],DI97->DI[0]);
	INT8U unitNum = 1;
	INT8U index;
	INT8U flagnegative[10] = {0};
	//电表日期
	if((DI97->DI[0] == 11)&&(DI97->DI[1] == 0xc0))
	{
		*dataType = dtdatetimes;
		unitNum = 1;
		INT16U year  = (DI97->Data[3] >> 4)*10 + (DI97->Data[3]&0x0f) + 2000;
		INT8U month = (DI97->Data[2] >> 4)*10 + (DI97->Data[2]&0x0f);
		INT8U day = (DI97->Data[1] >> 4)*10 + (DI97->Data[1]&0x0f);
		DI97->Data[0] = (year>>8)&0x00ff;
		DI97->Data[1] = year&0x00ff;
		DI97->Data[2] = month;
		DI97->Data[3] = day;
		return unitNum;
	}
	if((DI97->DI[0] == 10)&&(DI97->DI[1] == 0xc0))
	{
		*dataType = dtnull;
		unitNum = 1;
		INT8U hour  = (DI97->Data[2] >> 4)*10 + (DI97->Data[2]&0x0f);
		INT8U minute = (DI97->Data[1] >> 4)*10 + (DI97->Data[1]&0x0f);
		INT8U second = (DI97->Data[0] >> 4)*10 + (DI97->Data[0]&0x0f);
		DI97->Data[0] = hour;
		DI97->Data[1] = minute;
		DI97->Data[2] = second;
		return unitNum;
	}
	//需量类时间
	if((DI97->DI[1]==0xB0)||(DI97->DI[1]==0xB1)||(DI97->DI[1]==0xB4)
		||(DI97->DI[1]==0xB5)||(DI97->DI[1]==0xB8)||(DI97->DI[1]==0xB9))
	{
		*dataType = dtdatetimes;
		unitNum = 1;
		if((DI97->DI[0]&0x0f)==0x0f)
		{
			unitNum = 5;
		}
		INT8U unitIndex = 0;
		TS tsNow;
		TSGet(&tsNow);
		for(unitIndex = 0;unitIndex < unitNum; unitIndex++)
		{
			DI97->Data[unitIndex*7]= (tsNow.Year>>8)&0x00ff;
			DI97->Data[unitIndex*7+1] = tsNow.Year&0x00ff;
			DI97->Data[unitIndex*7+2] = (DI97->Data[unitIndex*4+3] >> 4)*10 + (DI97->Data[unitIndex*4+3]&0x0f);
			DI97->Data[unitIndex*7+3] = (DI97->Data[unitIndex*4+2] >> 4)*10 + (DI97->Data[unitIndex*4+2]&0x0f);
			DI97->Data[unitIndex*7+4] = (DI97->Data[unitIndex*4+1] >> 4)*10 + (DI97->Data[unitIndex*4+1]&0x0f);
			DI97->Data[unitIndex*7+5] = (DI97->Data[unitIndex*4] >> 4)*10 + (DI97->Data[unitIndex*4]&0x0f);
			DI97->Data[unitIndex*7+6] = 0;
			DI97->Length += 3;
		}
		return unitNum;
	}
	//需量类
	if((DI97->DI[1]==0xA0)||(DI97->DI[1]==0xA1)||(DI97->DI[1]==0xA4)
		||(DI97->DI[1]==0xA5)||(DI97->DI[1]==0xA8)||(DI97->DI[1]==0xA9))
	{
		*dataType = dtdoublelongunsigned;
		unitNum = 1;
		INT8U xuliangbuff[20] = {0};

		if((DI97->DI[0]&0x0f)==0x0f)
		{
			unitNum = 5;
		}
		INT8U unitIndex = 0;
		for(unitIndex = 0;unitIndex < unitNum; unitIndex++)
		{
			if((DI97->Data[unitIndex*3]==0xff)&&(DI97->Data[unitIndex*3+1]==0xff)&&(DI97->Data[unitIndex*3+2]==0xff))
			{
				memset(&xuliangbuff[unitIndex*4],0,4);
			}
			else
			{
				memcpy(&xuliangbuff[(unitIndex*4)+1],&DI97->Data[unitIndex*3],3);
			}
			DI97->Length += 1;
		}
		memcpy(&DI97->Data[0],xuliangbuff,20);
	}

	for (index = 0; index < map07DI_698OAD_NUM; index++)
	{
		if((map07DI_698OAD[index].flag97.DI_1[0][1]==DI97->DI[1])
				&&((map07DI_698OAD[index].flag97.DI_1[0][0]&0xf0)==(DI97->DI[0]&0xf0)))
			{
				*dataType = map07DI_698OAD[index].datatype;
				if((DI97->DI[0]&0x0f)==0x0f)
				{
					unitNum = map07DI_698OAD[index].unitnum;
				}
				else
				{
					unitNum = 1;
				}
				break;
			}
	}
	//电压　电流 功率 特殊处理  07回来的是3个字节  6984个字节

	if(DI97->DI[1]==0xb6)
	{
		//电压
		if((DI97->DI[0]&0xf0) == 0x10)
		{
			if((DI97->Data[2] == 0xff)&&(DI97->Data[3] == 0xff))
			{
				memset(&DI97->Data[2],0,2);
			}
			if((DI97->Data[4] == 0xff)&&(DI97->Data[5] == 0xff))
			{
				memset(&DI97->Data[4],0,2);
			}
		}
		//电流
		if((DI97->DI[0]&0xf0) == 0x20)
		{
			*dataType = dtdoublelong;
			INT8U f25_2_buff[16] = {0};

			INT8U tmpIndex = 0;
			for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
			{
				if((DI97->Data[tmpIndex*2]==0xff)&&(DI97->Data[tmpIndex*2+1]==0xff))
				{
					memset(&f25_2_buff[tmpIndex*4],0,4);
				}
				else
				{
					memcpy(&f25_2_buff[tmpIndex*4],&DI97->Data[tmpIndex*2],2);
				}
			}
			memcpy(&DI97->Data[0],f25_2_buff,16);
			if(unitNum == 1)
			{
				DI97->Length += 2;
			}
			else
			{
				DI97->Length += 10;
			}

		}

		if((DI97->DI[0]&0xf0) == 0x30)
		{
			*dataType = dtdoublelong;
			INT8U f25_3_buff[16] = {0};
			INT8U tmpIndex = 0;
			for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
			{
				if((DI97->Data[tmpIndex*3]==0xff)&&(DI97->Data[tmpIndex*3+1]==0xff)&&(DI97->Data[tmpIndex*3+2]==0xff))
				{
					memset(&f25_3_buff[tmpIndex*4],0,4);
				}
				else
				{
					if((DI97->Data[tmpIndex*3+2]&0x80) == 0x80)
					{
						DI97->Data[tmpIndex*3+2] = DI97->Data[tmpIndex*3+2]&0x7f;
						flagnegative[tmpIndex] = 1;
					}
					memcpy(&f25_3_buff[tmpIndex*4],&DI97->Data[tmpIndex*3],3);
				}
			}
			memcpy(DI97->Data,f25_3_buff,16);
			DI97->Length += unitNum;
		}


		//有功功率
		if((DI97->DI[0]&0xf0) == 0x40)
		{
			*dataType = dtdoublelong;
			INT8U f25_3_buff[16] = {0};
			INT8U tmpIndex = 0;
			for(tmpIndex = 0;tmpIndex < unitNum;tmpIndex++)
			{
				if((DI97->Data[tmpIndex*8]==0xff)&&(DI97->Data[tmpIndex*8+1]==0xff))
				{
					memset(&f25_3_buff[tmpIndex*4],0,4);
				}
				else
				{
					memcpy(&f25_3_buff[tmpIndex*4],&DI97->Data[tmpIndex*2],2);
				}
			}
			DI97->Length += unitNum*2;
		}

	}

	//功率因数
	if((DI97->DI[0]&0xf0) == 0x50)
	{
		if((DI97->Data[2] == 0xff)&&(DI97->Data[3] == 0xff))
		{
			memset(&DI97->Data[2],0,2);
		}
		if((DI97->Data[4] == 0xff)&&(DI97->Data[5] == 0xff))
		{
			memset(&DI97->Data[4],0,2);
		}
		if((DI97->Data[6] == 0xff)&&(DI97->Data[7] == 0xff))
		{
			memset(&DI97->Data[6],0,2);
		}

	}

	INT8U unitIndex;
	INT8U unitSize = (DI97->Length-2)/unitNum;
	fprintf(stderr,"DI07->Length = %d unitNum = %d unitSize = %d",DI97->Length,unitNum,unitSize);
	for(unitIndex = 0;unitIndex < unitNum; unitIndex++)
	{
		//目前07单个数据单元最大字节数为4
		INT8U reverBuff[4] = {0};
		INT32U value = 0;
		INT8U dataIndex = unitIndex*unitSize;
		bcd2int32u(&DI97->Data[dataIndex],unitSize,inverted,&value);
		if((DI97->DI[1]==0xb6)&&(((DI97->DI[0]&0xf0) == 0x10)||((DI97->DI[0]&0xf0) == 0x20)))
		{
			value = value*10;
		}
		fprintf(stderr,"\n value = %d",value);
		memcpy(&DI97->Data[dataIndex],&value,unitSize);

		if(unitSize == 2)
		{
			fprintf(stderr,"\n 123123  DI07->Data[%d] = %d  DI07->Data[%d+1] = %d",dataIndex,dataIndex,DI97->Data[dataIndex],DI97->Data[dataIndex+1]);
			INT8U tmpvalue = DI97->Data[dataIndex];
			DI97->Data[dataIndex] = DI97->Data[dataIndex+1];
			DI97->Data[dataIndex+1] = tmpvalue;
		}
		else
		{
			reversebuff(&DI97->Data[dataIndex], 4, reverBuff);
			memcpy(&DI97->Data[dataIndex],reverBuff,unitSize);
		}

	}

	return unitNum;
}

//把抄回来的07数据转换成698格式存储
INT16U data07Tobuff698(FORMAT07 Data07,INT8U* dataContent)
{
	INT16U index;

	INT16U len = 0;
	Base_DataType dataType = dtnull;
	INT8U unitSize = 0;
	INT8U unitNum = getASNInfo(&Data07,&dataType);
	if(unitNum == 0)
	{
		return 0;
	}
	INT16U dataLen07 = Data07.Length-4;
	#ifdef TESTDEF
		fprintf(stderr, "正常应答！  DI07 = %02x%02x%02x%02x datalen = %d data=",
				Data07.DI[3], Data07.DI[2], Data07.DI[1], Data07.DI[0],dataLen07);
		for(index = 0;index < dataLen07;index++)
		{
			fprintf(stderr," %02x",Data07.Data[index]);
		}
	#endif

	unitSize = dataLen07/unitNum;
	fprintf(stderr,"dataLen07 = %d unitNum = %d unitSize = %d",dataLen07,unitNum,unitSize);
	if((dataLen07%unitNum)!=0)
	{
		return len;
	}
	if(unitNum > 1)
	{
		dataContent[len++] = dtarray;
		dataContent[len++] = unitNum;
//		fprintf(stderr,"\n 1---- dataContent[%d] = %d",len,unitNum);
	}

	for(index = 0;index < unitNum;index++)
	{
		INT16U dataIndex = unitSize*index;
		if(dataType != dtnull)
		{
			dataContent[len++] = dataType;
		}
		fprintf(stderr,"\n 2---index = %d dataIndex = %d dataContent[%d] = %d",index,dataIndex,len,dataType);
		memcpy(&dataContent[len],&Data07.Data[dataIndex],unitSize);
		len += unitSize;
	}
#ifdef TESTDEF
	fprintf(stderr, "\n\n\n ###############data07Tobuff698[%d] = ",len);
	for(index = 0;index < len;index++)
	{
		fprintf(stderr," %02x",dataContent[index]);
	}
	fprintf(stderr, "###############\n\n\n");
#endif
	return len;
}
//把抄回来的07数据转换成698格式存储
INT16U data97Tobuff698(FORMAT97 Data97,INT8U* dataContent)
{
	INT16U index;

	INT16U len = 0;
	Base_DataType dataType = dtnull;
	INT8U unitSize = 0;
	//电量返回
	//68 75 00 00 00 00 00 68 81 1f 52 c3 63 c6 3a 34 5b 68 53 33 b7 c8 63 33 b4 8b 5b 33 6a 36 5b 33 33 33 33 33 33 33 33 33 dd 5f 16
	if((Data97.DI[1]==0x90)||(Data97.DI[1]==0x91)
		||(Data97.DI[1]==0xB0)||(Data97.DI[1]==0xB1)||(Data97.DI[1]==0xB4)
		||(Data97.DI[1]==0xB5)||(Data97.DI[1]==0xB8)||(Data97.DI[1]==0xB9))
	{
		Data97.Length = (Data97.Length > 22)?22:Data97.Length;
	}

	if((Data97.DI[1]==0xA0)||(Data97.DI[1]==0xA1)||(Data97.DI[1]==0xA4)
		||(Data97.DI[1]==0xA5)||(Data97.DI[1]==0xA8)||(Data97.DI[1]==0xA9))
	{
		Data97.Length = (Data97.Length > 17)?17:Data97.Length;
	}
	INT8U unitNum = getASNInfo97(&Data97,&dataType);
	if(unitNum == 0)
	{
		return 0;
	}

	INT16U dataLen97 = Data97.Length-2;

	#ifdef TESTDEF
		fprintf(stderr, "正常应答！  DI97 = %02x%02x datalen = %d data=", Data97.DI[1], Data97.DI[0],dataLen97);
		for(index = 0;index < dataLen97;index++)
		{
			fprintf(stderr," %02x",Data97.Data[index]);
		}
	#endif

	unitSize = dataLen97/unitNum;
	fprintf(stderr,"dataLen97 = %d unitNum = %d unitSize = %d",dataLen97,unitNum,unitSize);
	if((dataLen97%unitNum)!=0)
	{
		dataLen97 = dataLen97*unitNum;
	}
	if(unitNum > 1)
	{
		dataContent[len++] = dtarray;
		dataContent[len++] = unitNum;
//		fprintf(stderr,"\n 1---- dataContent[%d] = %d",len,unitNum);
	}

	for(index = 0;index < unitNum;index++)
	{
		INT16U dataIndex = unitSize*index;
		if(dataType != dtnull)
		{
			dataContent[len++] = dataType;
		}
		fprintf(stderr,"\n 2---index = %d dataIndex = %d dataContent[%d] = %d",index,dataIndex,len,dataType);
		memcpy(&dataContent[len],&Data97.Data[dataIndex],unitSize);
		len += unitSize;
	}
#ifdef TESTDEF
	fprintf(stderr, "\n\n\n ###############data97Tobuff698[%d] = ",len);
	for(index = 0;index < len;index++)
	{
		fprintf(stderr," %02x",dataContent[index]);
	}
	fprintf(stderr, "###############\n\n\n");
#endif
	return len;
}
INT16S request698_07DataSingle(FORMAT07* format07, INT8U* SendBuf,INT16S SendLen,
		CLASS_6035* st6035,INT8U* dataContent,CLASS_6001 meter,INT8U por485)
{
	BOOLEAN nextFlag = 0;
	INT8S recsta = 0;
	INT16S RecvLen = 0;
	INT8U RecvBuff[BUFFSIZE2048];
	INT16S buffLen = -1;
	memset(&RecvBuff[0], 0, BUFFSIZE2048);

	SendDataTo485(por485, SendBuf, SendLen);
	st6035->sendMsgNum++;
	RecvLen = ReceDataFrom485(DLT_645_07,por485, 500, RecvBuff);
	if (RecvLen > 0)
	{
		buffLen = 0;
		st6035->rcvMsgNum++;
		recsta = analyzeProtocol07(format07, RecvBuff, RecvLen, &nextFlag);
		if (recsta == 0)
		{
			//检查是否是事件关联数据标识
			if(format07->DI[3]==0x03)
			{
				buffLen = dealEventRecord(meter,*format07,st6035->taskID,dataContent);
			}
			else
			{
				//把07数据格式化放到dataContent
				buffLen = data07Tobuff698(*format07,dataContent);

				fprintf(stderr,"\n\n\ndataContent.Length = %d\n\n\n\n",buffLen);
				for(int i=0;i<buffLen;i++) {
					fprintf(stderr,"%02x ",dataContent[i]);
				}
				fprintf(stderr,"\n\n\n end  dataContent.Length = %d\n\n\n\n",buffLen);
				checkSumEnergy_07(meter,st6035->taskID,format07->DI,buffLen,dataContent);
				checkEvent(meter,*format07,st6035->taskID);
			}

		} else
		{

			if (recsta == -1) {
				fprintf(stderr, "电表异常应答，无数据项  %02x%02x%02x%02x！！！\n",
						format07->DI[3], format07->DI[2], format07->DI[1],
						format07->DI[0]);
			} else if (recsta == -2) {
				fprintf(stderr, "电表异常应答，未知错误！ Err=%02x\n", format07->Err);
			} else if (recsta == -3) {
				fprintf(stderr, "其他功能！\n");
			} else if (recsta == -4) {
				fprintf(stderr, "校验错误！\n");
			}
		}
	}
	fprintf(stderr,"request698_07DataSingle buffLen = %d",buffLen);
	return buffLen;
}
INT16S request698_97DataSingle(FORMAT97* format97, INT8U* SendBuf,INT16S SendLen,
		CLASS_6035* st6035,INT8U* dataContent,CLASS_6001 meter,INT8U por485)
{
	BOOLEAN nextFlag = 0;
	INT8S recsta = 0;
	INT16S RecvLen = 0;
	INT8U RecvBuff[BUFFSIZE2048];
	INT16S buffLen = -1;
	memset(&RecvBuff[0], 0, BUFFSIZE2048);

	SendDataTo485(por485, SendBuf, SendLen);
	st6035->sendMsgNum++;

	RecvLen = ReceDataFrom485(DLT_645_07,por485, 500, RecvBuff);

	if (RecvLen > 0)
	{
		buffLen = 0;
		st6035->rcvMsgNum++;
		recsta = analyzeProtocol97(format97, RecvBuff, RecvLen, &nextFlag);
		if (recsta == 0)
		{
			//把97数据格式化放到dataContent
			buffLen = data97Tobuff698(*format97,dataContent);

		} else
		{

			if (recsta == -1) {
				fprintf(stderr, "电表异常应答，无数据项  %02x%02x！！！\n",format97->DI[1],format97->DI[0]);
			} else if (recsta == -2) {
				fprintf(stderr, "电表异常应答，未知错误！ Err=%02x\n", format97->Err);
			} else if (recsta == -3) {
				fprintf(stderr, "其他功能！\n");
			} else if (recsta == -4) {
				fprintf(stderr, "校验错误！\n");
			}
		}
	}
	else if (RecvLen==-1)
	{
		fprintf(stderr, "485返回乱码！\n");
		recsta = -5;
	}
	else if (RecvLen==0)
	{
		fprintf(stderr, "485无返回数据！\n");
		recsta = -6;
	}
	fprintf(stderr,"request698_07DataSingle buffLen = %d",buffLen);
	return buffLen;
}
INT16S request698_07Data(INT8U* DI07,INT8U* dataContent,CLASS_6001 meter,CLASS_6035* st6035,INT8U port485)
{
	fprintf(stderr, "\n\n -----------request698_07Data-----------\n\n");
	INT16S retLen = -1;
	INT16S SendLen = 0;
	INT8U SendBuff[256];
	INT8U subindex = 0;
	INT8U invalidDI[4] = {0};
	FORMAT07 Data07;
	memset(&SendBuff[0], 0, BUFFSIZE256);
	memset(&Data07, 0, sizeof(FORMAT07));

	if (memcmp(invalidDI, DI07, 4) == 0)
	{
		fprintf(stderr,"\n 无效的数据标识");
		return retLen;
	}
	fprintf(stderr, "\n meterAddr len = %d addr = %02x%02x%02x%02x%02x%02x%02x%02x",
			meter.basicinfo.addr.addr[0], meter.basicinfo.addr.addr[1], meter.basicinfo.addr.addr[2],
			meter.basicinfo.addr.addr[3], meter.basicinfo.addr.addr[4], meter.basicinfo.addr.addr[5],
			meter.basicinfo.addr.addr[6],meter.basicinfo.addr.addr[7],meter.basicinfo.addr.addr[8]);
	fprintf(stderr, "\nDI = %02x%02x%02x%02x\n",DI07[0],DI07[1],DI07[2],DI07[3]);

	//DbgPrintToFile1(port485,"11111zeroBuff　%02x%02x%02x%02x",zeroBuff[0],zeroBuff[1],zeroBuff[2],zeroBuff[3]);

	Data07.Ctrl = CTRL_Read_07;
	if(meter.basicinfo.addr.addr[1] > 5)
	{
		meter.basicinfo.addr.addr[1] = 5;
		fprintf(stderr,"request698_07Data 电表地址长度大于6");
	}

	INT8U startIndex = 5 - meter.basicinfo.addr.addr[1];
	memcpy(&Data07.Addr[startIndex], &meter.basicinfo.addr.addr[2], (meter.basicinfo.addr.addr[1]+1));
	memcpy(&Data07.DI, DI07, 4);
	//DbgPrintToFile1(port485,"22222zeroBuff　%02x%02x%02x%02x",zeroBuff[0],zeroBuff[1],zeroBuff[2],zeroBuff[3]);
	DbgPrintToFile1(port485,"07测量点 = %02x%02x%02x%02x%02x%02x  DI = %02x%02x%02x%02x\n",
			Data07.Addr[0],Data07.Addr[1],Data07.Addr[2],Data07.Addr[3],Data07.Addr[4],Data07.Addr[5],
			DI07[0],DI07[1],DI07[2],DI07[3]);

	DEBUG_TIME_LINE("Ctrl: %02X", Data07.Ctrl);
	SendLen = composeProtocol07(&Data07, SendBuff);
	DEBUG_TIME_LINE("SendLen: %d", SendLen);
	if (SendLen < 0)
	{
		fprintf(stderr, "request698_07DataList1");
		return retLen;
	}
//	DbgPrintToFile1(port485,"33333zeroBuff　%02x%02x%02x%02x",zeroBuff[0],zeroBuff[1],zeroBuff[2],zeroBuff[3]);
	subindex = 0;
	while(subindex < MAX_RETRY_NUM)
	{
		retLen = request698_07DataSingle(&Data07,SendBuff,SendLen,st6035,dataContent,meter,port485);

		if(retLen >= 0)
		{
			return retLen;
		}
		subindex++;
	}
	//DbgPrintToFile1(port485,"444444zeroBuff　%02x%02x%02x%02x",zeroBuff[0],zeroBuff[1],zeroBuff[2],zeroBuff[3]);
	fprintf(stderr,"\n request698_07Data retLen = %d",retLen);
	return retLen;
}
INT16S request698_97Data(INT8U* DI97,INT8U* dataContent,CLASS_6001 meter,CLASS_6035* st6035,INT8U port485)
{
	fprintf(stderr, "\n\n -----------request698_97Data-----------\n\n");
	INT16S retLen = -1;
	INT16S SendLen = 0;
	INT8U SendBuff[256];
	INT8U subindex = 0;
	INT8U invalidDI[2] = {0};
	FORMAT97 Data97;
	memset(&SendBuff[0], 0, BUFFSIZE256);
	memset(&Data97, 0, sizeof(FORMAT07));

	if (memcmp(invalidDI, DI97, 2) == 0)
	{
		fprintf(stderr,"\n 无效的数据标识");
		return retLen;
	}
	fprintf(stderr, "\n meterAddr len = %d addr = %02x%02x%02x%02x%02x%02x%02x%02x",
			meter.basicinfo.addr.addr[0], meter.basicinfo.addr.addr[1], meter.basicinfo.addr.addr[2],
			meter.basicinfo.addr.addr[3], meter.basicinfo.addr.addr[4], meter.basicinfo.addr.addr[5],
			meter.basicinfo.addr.addr[6],meter.basicinfo.addr.addr[7],meter.basicinfo.addr.addr[8]);
	fprintf(stderr, "\nDI = %02x%02x\n",DI97[0],DI97[1]);

	Data97.Ctrl = CTRL_Read_97;
	if(meter.basicinfo.addr.addr[1] > 5)
	{
		meter.basicinfo.addr.addr[1] = 5;
		fprintf(stderr,"request698_97Data 电表地址长度大于6");
	}

	INT8U startIndex = 5 - meter.basicinfo.addr.addr[1];
	memcpy(&Data97.Addr[startIndex], &meter.basicinfo.addr.addr[2], (meter.basicinfo.addr.addr[1]+1));
	memcpy(&Data97.DI, DI97, 2);

	DbgPrintToFile1(port485,"97测量点 = %02x%02x%02x%02x%02x%02x  DI = %02x%02x\n",
			Data97.Addr[0],Data97.Addr[1],Data97.Addr[2],Data97.Addr[3],Data97.Addr[4],Data97.Addr[5],
			DI97[0],DI97[1]);

	SendLen = composeProtocol97(&Data97, SendBuff);
	if (SendLen < 0)
	{
		fprintf(stderr, "request698_07DataList1");
		return retLen;
	}

	subindex = 0;
	while(subindex < MAX_RETRY_NUM)
	{
		retLen = request698_97DataSingle(&Data97,SendBuff,SendLen,st6035,dataContent,meter,port485);

		if(retLen >= 0)
		{
			return retLen;
		}
		subindex++;
	}

	fprintf(stderr,"\n request698_07Data retLen = %d",retLen);
	return retLen;
}
/*
 * 698 OAD 和 645 07规约 数据标识转换
 * dir:0-通过698OAD找64507DI 1-通过64507DI找698OAD
 * */
INT8S OADMap07DI(OI_698 roadOI,OAD sourceOAD, C601F_645* flag645) {
//	DEBUG_TIME_LINE("INT8S OADMap07DI CSDMap07DI--------start------------------------------------------------------");
//	DEBUG_TIME_LINE("INT8S OADMap07DI roadOI = %04x sourceOAD = %04x%02x%02x\n",roadOI,sourceOAD.OI,sourceOAD.attflg,sourceOAD.attrindex);


	INT8S result = 0;
	INT8U index = 0;
	fprintf(stderr,"\nmap07DI_698OAD_NUM = %d",map07DI_698OAD_NUM);
	for (index = 0; index < map07DI_698OAD_NUM; index++)
	{
//		DEBUG_TIME_LINE("\n map07DI_698OAD[%d].roadOI = %04x OAD = %04x%02x%02x", index, map07DI_698OAD[index].roadOI
//				,map07DI_698OAD[index].flag698.OI,map07DI_698OAD[index].flag698.attflg,map07DI_698OAD[index].flag698.attrindex);
//
//		fprintf(stderr,"\nOI_1 =%04x OI_2 =%04x   ----------->  0I_11=%04x  0I_22=%04x",
//				roadOI,							sourceOAD.OI,
//				map07DI_698OAD[index].roadOI,	map07DI_698OAD[index].flag698.OI);
		if((memcmp(&roadOI,&map07DI_698OAD[index].roadOI,sizeof(OI_698))==0)
				&&(memcmp(&sourceOAD.OI,&map07DI_698OAD[index].flag698.OI,sizeof(OI_698))==0))
		{
//			DEBUG_TIME_LINE("");
			if(flag645->protocol == DLT_645_97)
			{
				if((map07DI_698OAD[index].flag97.DI_1[0][0]==0xff)&&(map07DI_698OAD[index].flag97.DI_1[0][1]==0xff))
				{
					fprintf(stderr,"\n 97数据项无效ffff");
					return 0;
				}
				fprintf(stderr,"\n 找到了对应97数据项　%02x%02x \n",map07DI_698OAD[index].flag97.DI_1[0][0],map07DI_698OAD[index].flag97.DI_1[0][1]);
				memcpy(&flag645->DI._97, &map07DI_698OAD[index].flag97, sizeof(C601F_07Flag));
			}
			if(flag645->protocol == DLT_645_07)
			{
				DEBUG_TIME_LINE("\n 找到了对应07数据项　%02x%02x%02x%02x \n",
						map07DI_698OAD[index].flag07.DI_1[0][0],map07DI_698OAD[index].flag07.DI_1[0][1],
						map07DI_698OAD[index].flag07.DI_1[0][2],map07DI_698OAD[index].flag07.DI_1[0][3]);
				memcpy(&flag645->DI._07, &map07DI_698OAD[index].flag07, sizeof(C601F_07Flag));
				if(sourceOAD.attrindex != 0x00)
				{
					if((sourceOAD.OI==0x2000)||(sourceOAD.OI==0x2001))
					{
						flag645->DI._07.DI_1[0][1] = sourceOAD.attrindex;
						//零序电流 02 80 00 01
						if((sourceOAD.OI==0x2001)&&((sourceOAD.attrindex==4)))
						{
							flag645->DI._07.DI_1[0][0] = 0x01;
							flag645->DI._07.DI_1[0][1] = 0x00;
							flag645->DI._07.DI_1[0][2] = 0x80;
						}
					}
					else
					{
						flag645->DI._07.DI_1[0][1] = sourceOAD.attrindex-1;
					}
				}

				if((sourceOAD.OI==0x2001)&&(sourceOAD.attflg==0x04) && sourceOAD.attrindex==0x00) {//不判断地区, 根据国网的规定来改
				//国网2017-08-04新标准, 零序电流的OAD改为 2001 0400, 对应的645-07 DI 为 02 80 00 01
						flag645->DI._07.DI_1[0][0] = 0x01;
						flag645->DI._07.DI_1[0][1] = 0x00;
						flag645->DI._07.DI_1[0][2] = 0x80;
						flag645->DI._07.DI_1[0][3] = 0x02;
						flag645->DI._07.dinum=1;
				}
			}
			return 1;
		}
	}

	fprintf(stderr, "\n CSDMap07DI--------没有对应的07数据项--------\n");
	return result;
}

INT8U getSinglegOADDataUnit(INT8U* oadData)
{
	INT8U length = 0;
	INT8U dataUnitLen = 0;
	switch(oadData[length])
	{
		case dtarray:
		{
			length++;
			INT8U arrayNum = oadData[length++];
			//fprintf(stderr,"\n arrayNum = %d\n",arrayNum);
			INT8U arrayNumIndex = 0;
			for(arrayNumIndex = 0;arrayNumIndex < arrayNum;arrayNumIndex++)
			{
				if((oadData[length] == dtarray)||(oadData[length] == dtstructure))
				{
					dataUnitLen = getSinglegOADDataUnit(&oadData[length]);
					//fprintf(stderr,"\n 1111  dataUnitLen = %d\n",dataUnitLen);
					length += dataUnitLen;
				}
				else
				{
					dataUnitLen = getBase_DataTypeLen(oadData[length],oadData[length+1]);
					length++;
					length += dataUnitLen;
				}
			}
		}
		break;
		case dtstructure:
		{
			length++;
			INT8U structMemberNum = oadData[length++];
			//fprintf(stderr,"\n structMemberNum = %d\n",structMemberNum);
			INT8U structMemberIndex = 0;
			for(structMemberIndex = 0;structMemberIndex < structMemberNum;structMemberIndex++)
			{
				dataUnitLen = getBase_DataTypeLen(oadData[length],oadData[length+1]);
			//	fprintf(stderr,"\n 22222  dataUnitLen = %d\n",dataUnitLen);
				length++;
				length += dataUnitLen;
			}
		}
		break;
		default:
		{
			dataUnitLen = getBase_DataTypeLen(oadData[length],oadData[length+1]);
			length++;
			length += dataUnitLen;
		}
	}
	return length;
}


INT8S checkEvent698(OAD rcvOAD,INT8U* data,INT8U dataLen,CLASS_6001 obj6001,INT16U taskID)
{
	 asyslog(LOG_INFO,"taskID = %d event_obj.task_no　= %d checkEvent698 测量点 = %02x%02x%02x%02x%02x%02x%02x%02x  rcvOI= %04x dataLen = %d data = %02x%02x%02x%02x%02x%02x%02x%02x\n",
			 taskID,JProgramInfo->event_obj.Event311C_obj.task_para.task_no,obj6001.basicinfo.addr.addr[0],obj6001.basicinfo.addr.addr[1],obj6001.basicinfo.addr.addr[2],obj6001.basicinfo.addr.addr[3],
			obj6001.basicinfo.addr.addr[4],obj6001.basicinfo.addr.addr[5],obj6001.basicinfo.addr.addr[6],obj6001.basicinfo.addr.addr[7],
			rcvOAD.OI,dataLen,data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7]);

	INT8S ret = -1;
	if(rcvOAD.OI == 0x4000)
	{
		ret = Event_3105(obj6001.basicinfo.addr,taskID,&data[1],dataLen,JProgramInfo);
	}
	if((JProgramInfo->cfg_para.device == SPTF3)&&((rcvOAD.OI == 0x0010)||(rcvOAD.OI == 0x0020)))
	{
		//更新总加组电量
		OAD oadCur = {0};
		fillclass23data(oadCur,rcvOAD,obj6001.basicinfo.addr,data,JProgramInfo);
	}
	if(rcvOAD.OI == 0x0010)
	{
		ret = Event_310B(obj6001.basicinfo.addr,taskID,&data[3],dataLen,JProgramInfo);

		ret = Event_310C(obj6001.basicinfo.addr,taskID,&data[3],dataLen,JProgramInfo,obj6001);

		ret = Event_310D(obj6001.basicinfo.addr,taskID,&data[3],dataLen,JProgramInfo,obj6001);

		ret = Event_310E(obj6001.basicinfo.addr,taskID,&data[3],dataLen,JProgramInfo);
	}
	fprintf(stderr,"taskID = %d, task_no=%d\n",taskID,JProgramInfo->event_obj.Event311C_obj.task_para.task_no);
//	if(taskID == JProgramInfo->event_obj.Event311C_obj.task_para.task_no)	//文件更改未更新共享内存的task_no，去掉判断，在产生事件内部判断
//	{
//		fprintf(stderr,"checkEvent698_311C OI = %02x",rcvOAD.OI);
		ret = Event_311C(obj6001.basicinfo.addr,taskID,rcvOAD,data,dataLen,JProgramInfo);
//	}

	return ret;
}
/*
 * 解析单个OAD数据
 * */

INT8U parseSingleOADData(INT8U* oadData,OADDATA_SAVE* oadListContent)
{
	fprintf(stderr,"\n --------------------------parseSingleOADData-------------------------\n");
	INT8U dataLen = 0;
	INT16U length = 0;
	fprintf(stderr,"\n OAD %02x %02x %02x %02x\n",oadData[length],oadData[length+1],oadData[length+2],oadData[length+3]);

	OAD rcvOAD;
	rcvOAD.OI = (oadData[length] << 8) + oadData[length+1];
	rcvOAD.attflg = oadData[length+2];
	rcvOAD.attrindex = oadData[length+3];

	length += 4;

	if(oadData[length++] == 0)//0------ 没有数据  1--数据
	{
		length++;//DAR
		return length;
	}
	else
	{
		INT16U startIndex = 0;
		startIndex = length;
		dataLen = getSinglegOADDataUnit(&oadData[length]);
		length += dataLen;
		oadListContent->datalen = dataLen;
		memcpy(&oadListContent->oad_r,&rcvOAD,sizeof(OAD));
		memcpy(oadListContent->data,&oadData[startIndex],dataLen);

		fprintf(stderr,"\n dataLen = %d\n",dataLen);
	}

#ifdef TESTDEF
	INT16U prtIndex =0;
	fprintf(stderr,"parseSingleOADData dataContent[%d] = ",dataLen);
	for(prtIndex = 0;prtIndex < dataLen;prtIndex++)
	{
		fprintf(stderr,"%02x ",oadListContent->data[prtIndex]);
	}
#endif

	return length;
}
INT8U parseSingleROADDataHead(INT8U* oadData,OADDATA_SAVE* oadListContent,INT8U* rcvCSDnum,INT8U* recordNum)
{
	fprintf(stderr,"\n --------------------------parseSingleROADData-----Head-------------------------\n");
	INT8U length = 0;

	fprintf(stderr,"\n receive ROAD %02x %02x %02x %02x\n",oadData[length],oadData[length+1],oadData[length+2],oadData[length+3]);
	OAD rcvOAD_m;
	rcvOAD_m.OI = (oadData[length] << 8) + oadData[length+1];
	rcvOAD_m.attflg = oadData[length+2];
	rcvOAD_m.attrindex = oadData[length+3];

	length += 4;

	*rcvCSDnum = oadData[length++];//csd数量
	fprintf(stderr,"\n rcvCSDnum = %d",*rcvCSDnum);
	if((*rcvCSDnum > ROAD_OADS_NUM)||(*rcvCSDnum == 0))
	{
		return length;
	}
	INT8U csdIndex;
	fprintf(stderr,"\n 收到回复的OAD ：");
	for(csdIndex = 0;csdIndex < *rcvCSDnum;csdIndex++)
	{
		length++;
		memcpy(&oadListContent[csdIndex].oad_m,&rcvOAD_m,sizeof(OAD));
		oadListContent[csdIndex].oad_r.OI = (oadData[length] << 8) + oadData[length+1];
		oadListContent[csdIndex].oad_r.attflg = oadData[length+2];
		oadListContent[csdIndex].oad_r.attrindex = oadData[length+3];
		fprintf(stderr," /%d-%04x%02x%02x ",csdIndex,oadListContent[csdIndex].oad_r.OI,oadListContent[csdIndex].oad_r.attflg,oadListContent[csdIndex].oad_r.attrindex);
		length += 4;
	}

	INT8U Dar = oadData[length];
	if(Dar == 0)// 1-数据  0-错误
	{
		return length;
	}
	length++;
	*recordNum = oadData[length];
	fprintf(stderr,"\n recordNum = %d",*recordNum);
	length++;
	return length;
}
/*
 * 解析单个ROAD数据
 * 数据为空按road格式补0
 * */

INT16U parseSingleROADDataBody(INT8U* oadData,OADDATA_SAVE* oadListContent,INT8U rcvCSDnum)
{
	fprintf(stderr,"\n --------------------------parseSingleROADData-----Body-------------------------\n");

	//解析收到的ROAD数据
	INT8U csdIndex = 0;
	INT16U startIndex = 0,length = 0;
	for(csdIndex = 0;csdIndex < rcvCSDnum;csdIndex++)
	{
		startIndex = length;
		INT8U singledataLen = getSinglegOADDataUnit(&oadData[length]);
		length += singledataLen;
		oadListContent[csdIndex].datalen = singledataLen;
		memcpy(oadListContent[csdIndex].data,&oadData[startIndex],singledataLen);
#if 1
		fprintf(stderr,"\n--------receive content -------\n");
		fprintf(stderr,"\nOAD = %04x%02x%02x",oadListContent[csdIndex].oad_r.OI,oadListContent[csdIndex].oad_r.attflg,oadListContent[csdIndex].oad_r.attrindex);
		fprintf(stderr,"\ndatalen = %d",singledataLen);
		fprintf(stderr,"\ncontent = ");

		INT8U prtIndex = 0;
		for(prtIndex = 0;prtIndex < singledataLen;prtIndex++)
		{
			fprintf(stderr,"%02x ",oadListContent[csdIndex].data[prtIndex]);
		}
#endif
	}


	return length;
}

INT8U checkTimeStamp698(OADDATA_SAVE oadListContent[ROAD_OADS_NUM])
{
	INT8U ret = 1;
	INT8U oadIndex = 0;
	for(oadIndex = 0;oadIndex < ROAD_OADS_NUM;oadIndex++)
	{
		if(oadListContent[oadIndex].oad_r.OI==0x2021)
		{
			DbPrt1(2,"checkTimeStamp698 buff:", (char *) oadListContent[oadIndex].data, 10, NULL);
			return isTimerSame(0,oadListContent[oadIndex].data);
		}
	}
	return ret;
}

/*
 *解析698抄表返回的报文
 *返回oadListContent
 * */
INT8U deal698RequestResponse(INT8U getResponseType,INT8U csdNum,INT8U* apdudata,OADDATA_SAVE* oadListContent,INT16U* apdudataLen)
{
	INT16U apdudataIndex =0;
	INT8U dataCount =0;

	INT16U oaddataLen = 0;

	switch(getResponseType)
	{
	case GET_REQUEST_NORMAL:
		{
			oaddataLen = parseSingleOADData(&apdudata[apdudataIndex],oadListContent);
			if(oaddataLen > 6)
			{
				dataCount++;
			}
		}
		break;

		case GET_REQUEST_NORMAL_LIST:
		{
			INT8U oadIndex;
			for(oadIndex=0;oadIndex < csdNum;oadIndex++)
			{
				oaddataLen = parseSingleOADData(&apdudata[apdudataIndex],&oadListContent[dataCount]);
				apdudataIndex += oaddataLen;
				if(oaddataLen > 6)
				{
					dataCount++;
				}
				if(dataCount >= OADS_NUM_485_ONCEREAD)
				{
					asyslog(LOG_NOTICE,"dataCount dataCount > OADS_NUM_485_ONCEREAD");
					break;
				}
			}
		}
		break;
		case GET_REQUEST_RECORD:
		{
			INT8U rcvCSDnum = 0,recordNum = 0;

			oaddataLen = parseSingleROADDataHead(&apdudata[apdudataIndex],oadListContent,&rcvCSDnum,&recordNum);
			apdudataIndex += oaddataLen;

			if(rcvCSDnum > 0 && recordNum > 0)
			{
				INT8U recordIndex = 0;
				for(recordIndex = 0;recordIndex < recordNum;recordIndex++)
				{
					oaddataLen = parseSingleROADDataBody(&apdudata[apdudataIndex],&oadListContent[dataCount],rcvCSDnum);
					apdudataIndex += oaddataLen;
					dataCount += rcvCSDnum;
				}
				if(dataCount >= OADS_NUM_485_ONCEREAD)
				{
					asyslog(LOG_NOTICE,"dataCount dataCount > OADS_NUM_485_ONCEREAD");
					break;
				}
			}


		}

		break;
	}
	*apdudataLen = apdudataIndex;
	return dataCount;
}
INT16U dealProxy_Record_698(PROXY_GETLIST *getlist,INT8U* dataContent,INT8U port485)
{
	/* TSA
	 * PIID
	 * GetRecord-
	 * 			|__OAD
	 * 			|__RSD
	 * 			|__RCSD
	 */
	INT16U retdataLen = 0;
	time_t timenow;
	RSD rsd={};
	INT16U TxLen=0;
	INT8U sendbuff[BUFFSIZE256];
	INT8U recvbuff[BUFFSIZE2048];
	memset(sendbuff,0,BUFFSIZE256);
	memset(recvbuff,0,BUFFSIZE2048);

	TxLen = composeProtocol698_GetRequestRecord(getlist,sendbuff);
	SendDataTo485(port485, sendbuff, TxLen);
	while(1)
	{
		sleep(1);
		timenow = time(NULL);
		TxLen = ReceDataFrom485(DLT_698,port485, 500, recvbuff);
		if(TxLen > 0)
		{
			INT8U csdNum = 0;
			INT16S dataLen = TxLen;
			INT8U apduDataStartIndex = 0;
			INT8U getResponseType = analyzeProtocol698(recvbuff,&csdNum,dataLen,&apduDataStartIndex,&dataLen);
			if (getResponseType == GET_REQUEST_RECORD)
			{
				memcpy(dataContent,&recvbuff[apduDataStartIndex],dataLen);
				retdataLen = dataLen -2;
			}
		}
		if (abs(timenow - getlist->timeold)>=getlist->timeout)
		{
			fprintf(stderr,"\n read485 ： 代理下发过程超时");
			break;
		}
	}
	fprintf(stderr,"retdataLen=%d\n",retdataLen);
	return retdataLen;
}
INT16U dealProxy_SET_ACTION_698(INT8U type,ACTION_SET_OBJ setOBJ,INT8U* dataContent,INT8U port485)
{
	INT16U retdataLen = 0;
	INT16S sendLen = 0;
	INT16S recvLen = 0;
	INT8U sendbuff[BUFFSIZE256];
	INT8U recvbuff[BUFFSIZE2048];

	memset(sendbuff, 0, BUFFSIZE256);
	memset(recvbuff, 0, BUFFSIZE2048);
	sendLen = composeProtocol698_SetActionRequest(sendbuff,type,setOBJ);
	if(sendLen < 0)
	{
		fprintf(stderr,"deal6015_698  sendLen < 0");
		return retdataLen;
	}

	INT8U subindex = 0;
	while(subindex < MAX_RETRY_NUM)
	{
		memset(recvbuff, 0, BUFFSIZE2048);
		SendDataTo485(port485, sendbuff, sendLen);

		recvLen = ReceDataFrom485(DLT_698,port485, 1000, recvbuff);
		if(recvLen > 0)
		{
			INT8U csdNum = 0;
			INT16S dataLen = recvLen;
			INT8U apduDataStartIndex = 0;
			INT8U getResponseType = analyzeProtocol698(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
			fprintf(stderr,"\n dealProxy_SET_ACTION_698 getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);
			memcpy(dataContent,&recvbuff[apduDataStartIndex],dataLen);
			retdataLen = dataLen-2;
			break;
		}
		subindex++;
	}
	return retdataLen;
}
INT16U dealProxy_SET_ACTION_THEN_GET698(INT8U type,DO_Then_GET DOGETOBJ,INT8U* dataContent,INT8U port485)
{
	INT16U retdataLen = 0;
	INT16S sendLen = 0;
	INT16S recvLen = 0;
	INT8U sendbuff[BUFFSIZE256];
	INT8U recvbuff[BUFFSIZE2048];

	memset(sendbuff, 0, BUFFSIZE256);
	memset(recvbuff, 0, BUFFSIZE2048);
	sendLen = composeProtocol698_SetActionThenGetRequest(sendbuff,type,DOGETOBJ);
	if(sendLen < 0)
	{
		fprintf(stderr,"deal6015_698  sendLen < 0");
		return retdataLen;
	}

	INT8U subindex = 0;
	while(subindex < MAX_RETRY_NUM)
	{
		memset(recvbuff, 0, BUFFSIZE2048);
		SendDataTo485(port485, sendbuff, sendLen);

		recvLen = ReceDataFrom485(DLT_698,port485, 1000, recvbuff);
		if(recvLen > 0)
		{
			INT8U csdNum = 0;
			INT16S dataLen = recvLen;
			INT8U apduDataStartIndex = 0;
			INT8U getResponseType = analyzeProtocol698(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
			fprintf(stderr,"\n dealProxy_SET_ACTION_698 getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);
			memcpy(dataContent,&recvbuff[apduDataStartIndex],dataLen);
			retdataLen = dataLen -2;
			break;
		}
		subindex++;
	}
	return retdataLen;
}
INT16U dealProxy_698(CLASS_6001 obj6001,GETOBJS obj07,INT8U* dataContent,INT8U port485)
{
	DbgPrintToFile1(port485," 处理698测量点 代理消息 dealProxy_698 obj07.num = %d",obj07.num);
	fprintf(stderr," 处理698测量点 代理消息 dealProxy_698 obj07.num = %d",obj07.num);

	CLASS_6015 st6015;
	memset(&st6015,0,sizeof(CLASS_6015));
	st6015.cjtype = TYPE_NULL;
	st6015.csds.num = obj07.num;
	INT8U csdIndex;
	for(csdIndex = 0;csdIndex < st6015.csds.num;csdIndex++)
	{
		st6015.csds.csd[csdIndex].type = 0;
		memcpy(&st6015.csds.csd[csdIndex].csd.oad,&obj07.oads[csdIndex],sizeof(OAD));
		DbgPrintToFile1(port485," OAD[%d] = %04x%02x%02x",csdIndex,st6015.csds.csd[csdIndex].csd.oad.OI,
				st6015.csds.csd[csdIndex].csd.oad.attflg,st6015.csds.csd[csdIndex].csd.oad.attrindex);
	}
	INT16S sendLen = 0;
	INT16S recvLen = 0;
	INT16U retdataLen = 0;
	INT8U sendbuff[BUFFSIZE128];
	INT8U recvbuff[BUFFSIZE2048];

	memset(sendbuff, 0, BUFFSIZE128);

	sendLen = composeProtocol698_GetRequest(sendbuff, st6015,obj6001.basicinfo.addr);
	if(sendLen < 0)
	{
		fprintf(stderr,"deal6015_698  sendLen < 0");
		return retdataLen;
	}

	INT8U subindex = 0;
	while(subindex < MAX_RETRY_NUM)
	{
		memset(recvbuff, 0, BUFFSIZE2048);
		SendDataTo485(port485, sendbuff, sendLen);

		recvLen = ReceDataFrom485(DLT_698,port485, 1000, recvbuff);
		if(recvLen > 0)
		{
			INT8U csdNum = 0;
			INT16S dataLen = recvLen;
			INT8U apduDataStartIndex = 0;
			INT8U getResponseType = analyzeProtocol698(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
			fprintf(stderr,"\n dealProxy_698 getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);
			if((getResponseType == GET_REQUEST_NORMAL_LIST)||(getResponseType == GET_REQUEST_NORMAL))
			{
				OADDATA_SAVE oadListContent[ROAD_OADS_NUM];
				memset(oadListContent,0,ROAD_OADS_NUM*sizeof(OADDATA_SAVE));
				INT16U apdudatalen = 0;
				deal698RequestResponse(getResponseType,csdNum,&recvbuff[apduDataStartIndex],oadListContent,&apdudatalen);
				retdataLen = dataLen - 2;
				memcpy(dataContent,&recvbuff[apduDataStartIndex],retdataLen);
				break;
			}
			else
			{
				retdataLen = 0;
			}
		}else
		{
			obj07.dar = request_overtime;	//消息队列最后组帧，要组织多个OAD的值
			retdataLen = 0;
//			OADtoBuff(obj07.oads[0],&dataContent[dataLen]);
//			create_OAD(0,&dataContent[dataLen],obj07.oads[0]);
//			dataLen += sizeof(OAD);
//			dataContent[dataLen++] = 0x00;//错误，没有数据
//			dataContent[dataLen++] = request_overtime;//DARType
//			return dataLen;
		}
		subindex++;
	}

	return retdataLen;
}
INT16U dealProxy_645_07(GETOBJS obj07,INT8U* dataContent,INT8U port485,INT16U timeout)
{
	INT16S singledataLen = -1;
	INT16U dataLen = 0;
	INT16U dataFlagPos = 0;
	CLASS_6001 meter = {};
	CLASS_6035 inValid6035 = {};

	INT8U oadIndex;
	INT8U diIndex;

	for(oadIndex = 0;oadIndex < obj07.num;oadIndex++)
	{
		DbgPrintToFile1(port485," OAD[%d] = %04x%02x%02x",oadIndex,obj07.oads[oadIndex].OI,obj07.oads[oadIndex].attflg,obj07.oads[oadIndex].attrindex);
//		OADtoBuff(obj07.oads[oadIndex],&dataContent[dataLen]);
//		dataLen += sizeof(OAD);
		dataLen += create_OAD(0,&dataContent[dataLen],obj07.oads[oadIndex]);
		C601F_645 Flag645;
		memset(&Flag645,0,sizeof(C601F_645));
		Flag645.protocol = DLT_645_07;
		if(OADMap07DI(0x0000,obj07.oads[oadIndex],&Flag645)!=1)
		{
			dataContent[dataLen++] = 0x00;
			dataContent[dataLen++] = 0x21;
			asyslog(LOG_WARNING,"dealProxy_645_07 找不到%04x%02x%02x对应07数据项",obj07.oads[oadIndex].OI,obj07.oads[oadIndex].attflg,obj07.oads[oadIndex].attrindex);
			fprintf(stderr,"\n 找不到%04x%02x%02x对应07数据项",obj07.oads[oadIndex].OI,obj07.oads[oadIndex].attflg,obj07.oads[oadIndex].attrindex);
			continue;
		}
		memcpy(meter.basicinfo.addr.addr,obj07.tsa.addr,sizeof(TSA));
		dataFlagPos = dataLen;
		dataContent[dataLen++] = 0x01;//默认有数据

		for(diIndex=0;diIndex<Flag645.DI._07.dinum;diIndex++)
		{
			singledataLen = request698_07Data(Flag645.DI._07.DI_1[diIndex],&dataContent[dataLen],meter,&inValid6035,port485);

			if(singledataLen >= 0)
			{
				dataLen += singledataLen;
			}
#if 0
			time_t nowtime = time(NULL);
			if(nowtime > (starttime + timeout))
			{
		        asyslog(LOG_WARNING, "dealProxy_645_07 time out");
				return 0;
			}
#endif
		}
		if(dataLen == (dataFlagPos+1))
		{
			dataContent[dataFlagPos] = 0x00;//没有数据
			dataContent[dataLen++] = 0x21;//DARType

			fprintf(stderr,"\n没有数据 dataLen=%d",dataLen);
		}
	}
	fprintf(stderr,"\n 处理07测量点代理返回 dealProxy_645_07 dataLen = %d",dataLen);
	return dataLen;
}
void set_port_active(INT8U port485,INT8U value)
{
	if (port485==1)
		proxyInUse.devUse.rs485_1_Active = value;
	if (port485==2)
		proxyInUse.devUse.rs485_2_Active = value;
}
INT8S dealProxyType2(PROXY_GETLIST *getlist,INT8U port485)
{
	INT8U addrlen=0;
	int dataindex=0;
	INT8S result = -1;
	INT16U singleLen = 0;
	INT8U tmpbuf[512]={};
	CLASS_6001 obj6001 = {};

	if( get6001ObjByTSA(getlist->proxy_obj.record.tsa,&obj6001) != 1 ||
	    obj6001.basicinfo.port.attrindex != port485 ||
	    getComfdBy6001(obj6001.basicinfo.baud,obj6001.basicinfo.port.attrindex) != 1	)
	{
		set_port_active(port485,0);
		return -1;
	}
	fprintf(stderr,"\n *************getlist->data[%d]: ",getlist->proxylen);
	INT8U prtIndex = 0;

	for(prtIndex = 0;prtIndex<getlist->proxylen;prtIndex++)
	{
		fprintf(stderr,"%02x ",getlist->proxy_obj.buf[prtIndex]);
	}
	INT8U portUse = obj6001.basicinfo.port.attrindex;
	switch(obj6001.basicinfo.protocol)
	{
		case DLT_645_07:
//			singleLen = dealProxy_645_07(getlist->proxy_obj.objs[index],tmpbuf,portUse,getlist->proxy_obj.objs[index].onetimeout);
			break;
		default:
			singleLen = dealProxy_Record_698(getlist ,tmpbuf,portUse);
			break;
	}
	pthread_mutex_lock(&mutex);
	fprintf(stderr,"\nTSA 返回长度 singleLen=%d\n",singleLen);
	dataindex= getlist->datalen;
	addrlen = getlist->proxy_obj.record.tsa.addr[0]+1;
	fprintf(stderr,"\nTSA addr长度 addrlen=%d\n",addrlen);
	memcpy(&getlist->data[dataindex],&getlist->proxy_obj.record.tsa.addr[0],addrlen);
	dataindex += addrlen;
	fprintf(stderr,"\nTSA buf 指针 dataindex=%d\n",dataindex);
	for(prtIndex=0;prtIndex<dataindex;prtIndex++) {
		fprintf(stderr,"%02x ",getlist->data[prtIndex]);
	}
	if(singleLen > 0)
	{
		memcpy(&getlist->data[dataindex],tmpbuf,singleLen);
		dataindex += singleLen;
		getlist->datalen += dataindex;
	}else
	{
		getlist->proxy_obj.record.dar = request_overtime;
		getlist->datalen = 0;
//		getlist->data[dataindex++] = 0;
//		getlist->datalen += dataindex;
	}
	pthread_mutex_unlock(&mutex);
	set_port_active(port485,0);
	return result;
}
INT8S dealProxyType1(PROXY_GETLIST *getlist,INT8U port485)
{
	INT8U addrlen=0;
	int dataindex=0;
	INT8S result = -1;
	INT8U index;
	INT16U singleLen = 0;
	INT8U tmpbuf[512]={};
	CLASS_6001 obj6001 = {};
	fprintf(stderr,"\n RS485-%d 判断代理对象(TSA)数量 objs num = %d :",port485,getlist->num);

	for(index = 0;index < getlist->num;index++)
	{
		if (proxyInUse.devUse.rs485Need == 0)
			break;
		if( get6001ObjByTSA(getlist->proxy_obj.objs[index].tsa,&obj6001) != 1 ||
		    obj6001.basicinfo.port.attrindex != port485 ||
		    getComfdBy6001(obj6001.basicinfo.baud,obj6001.basicinfo.port.attrindex) != 1)
		{
			continue;
		}
		fprintf(stderr,"\nRS485-%d 合法检查通过！ obj-%d ",port485,index);
		INT8U portUse = obj6001.basicinfo.port.attrindex;
		switch(obj6001.basicinfo.protocol)
		{
			case DLT_645_07:
				singleLen = dealProxy_645_07(getlist->proxy_obj.objs[index],tmpbuf,portUse,getlist->proxy_obj.objs[index].onetimeout);
				break;
			default:
				singleLen = dealProxy_698(obj6001,getlist->proxy_obj.objs[index],tmpbuf,portUse);
		}
		pthread_mutex_lock(&mutex);
		fprintf(stderr,"\nTSA 返回长度 singleLen=%d\n",singleLen);
		dataindex= getlist->datalen;
		addrlen = getlist->proxy_obj.objs[index].tsa.addr[0]+1;
		fprintf(stderr,"\nTSA addr长度 addrlen=%d\n",addrlen);
		memcpy(&getlist->data[dataindex],&getlist->proxy_obj.objs[index].tsa.addr[0],addrlen);
		dataindex += addrlen;
		getlist->data[dataindex++] = getlist->proxy_obj.objs[index].num;
		fprintf(stderr,"\nTSA buf 指针 dataindex=%d\n",dataindex);

		if(singleLen > 0)
		{
			getlist->proxy_obj.objs[index].dar = proxy_success;
			memcpy(&getlist->data[dataindex],tmpbuf,singleLen);
			dataindex += singleLen;
			getlist->datalen += dataindex;
		}else {
			getlist->proxy_obj.objs[index].dar = request_overtime;
		}
//		else	//TODO：？？？没有数据放在最后统一处理
//		{
//			getlist->data[dataindex++] = 0;
//			getlist->datalen += dataindex;
//		}
		pthread_mutex_unlock(&mutex);
	}
	set_port_active(port485,0);

	return result;
}
INT8S dealProxyType7(PROXY_GETLIST *getlist,INT8U port485)
{
	INT8S result = -1;
	CSINFO csinfo={};
	int hcsok = 0 ,fcsok = 0;

	fprintf(stderr,"\n代理 7 !!!");

	if(is485OAD(getlist->proxy_obj.transcmd.oad,port485) != 1)
	{
		return result;
	}
	char* par[3]= {"none","odd","even"};

	INT32U baudrate = getMeterBaud(getlist->proxy_obj.transcmd.comdcb.baud);

	comfd485[port485-1] = open_com_para_chg(port485, baudrate, comfd485[port485-1],
			(unsigned char*)par[getlist->proxy_obj.transcmd.comdcb.verify], getlist->proxy_obj.transcmd.comdcb.stopbits,
			getlist->proxy_obj.transcmd.comdcb.databits);

	if (comfd485[port485-1] <= 0)
	{
		fprintf(stderr, "dealProxyType7 打开S485%d串口失败\n",port485);
		return result;
	}
#if 0
	fprintf(stderr,"代理7:内容长度[%d]=",getlist->proxy_obj.transcmd.cmdlen);
	INT8U prtIndex =0;
	for(prtIndex=0;prtIndex<getlist->proxy_obj.transcmd.cmdlen;prtIndex++)
	{
		fprintf(stderr,"%02x ",getlist->proxy_obj.transcmd.cmdbuf[prtIndex]);
	}
#endif
	INT8U RecvBuff[BUFFSIZE1024];
	INT8U TmprevBuf[BUFFSIZE1024];
	INT16S RecvLen = 0;
	INT16U	dindex = 0;

	memset(&RecvBuff[0], 0, BUFFSIZE1024);
	memset(&TmprevBuf[0], 0, BUFFSIZE1024);
	memset(&csinfo,0,sizeof(CSINFO));
	hcsok = CheckHead(getlist->proxy_obj.transcmd.cmdbuf ,&csinfo);
	fcsok = CheckTail(getlist->proxy_obj.transcmd.cmdbuf ,csinfo.frame_length);
	//if ((hcsok==1) && (fcsok==1))
	{	//TODO:一致性测试PROXY_36透传一个错误报文，导致台体模拟表软件停抄，故简单判断合法性进行报文透传
		SendDataTo485(port485, getlist->proxy_obj.transcmd.cmdbuf, getlist->proxy_obj.transcmd.cmdlen);
		INT32S fd = comfd485[port485-1];
		usleep(20000);	//20ms
		INT16U i= 0,j = 0,len = 0,rec_head = 0;
		//TODO: 此处延时是否应该按照代理下发的超时时间进行处理？
		//getlist->proxy_obj.transcmd.bytetimeout,getlist->proxy_obj.transcmd.revtimeout
		for (j = 0; j < 20; j++)
		{
			usleep(20000);	//20ms
			len = read(fd, TmprevBuf,BUFFSIZE1024);

			if (len > 0) {
				RecvLen += len;
				if (RecvLen > BUFFSIZE1024) {
					fprintf(stderr, "len_Total=%d, xxxxxxxxxxx\n", RecvLen);
					break;
				}
				for (i = 0; i < len; i++) {
					RecvBuff[rec_head++] = TmprevBuf[i];
				}
			}
		}
		fprintf(stderr,"\n 代理7 串口返回 %d 字节",RecvLen);
		DbPrt1(port485,"代理透传返回:", (char *) RecvBuff, RecvLen, NULL);
		if(getZone("GW")==0) {
			char title[20];
			sprintf(title,"[485_%d]R:",port485);
			bufsyslog(RecvBuff, title, len, 0, BUFFSIZE1024);
		}
	}

	if(RecvLen > 0)	{
		fprintf(stderr,"\n代理透传　RecvLen = %d\n",RecvLen);
		getlist->proxy_obj.transcmd.dar = success;
		dindex = 0;
		getlist->data[dindex++] = 1;
		//代理应答返回数据类型为octet-string
		dindex += fillStringLen(&getlist->data[dindex],RecvLen);
		memcpy(&getlist->data[dindex],&RecvBuff,RecvLen);
		dindex += RecvLen;
		getlist->datalen = dindex;
		DbgPrintToFile1(port485,"代理返回数据长度 = %d Data=%02X [%02x_%02x_%02x]",dindex,getlist->data[0],
				getlist->data[1],getlist->data[2],getlist->data[3]);


	}else {
		getlist->proxy_obj.transcmd.dar = request_overtime;
		getlist->datalen = 0;
		//dealAnswer 统一处理
//		getlist->data[0] = 0;
//		getlist->data[1] = request_overtime;//DAR
//		getlist->datalen = 2;
		fprintf(stderr,"\nproxy request overtime.....getlist->datalen=%d\n",getlist->datalen);
	}
	proxyInUse.devUse.rs485Ready  = 1;
	//set_port_active(port485,0);
	fprintf(stderr,"\ndealProxyType7 RecvLen=%d request overtime.....getlist->datalen=%d\n",RecvLen,getlist->datalen);

	return result;
}

/*
 * ProxySetRequestList 代理设置若干个服务器的若干个对象属性请求
 * */
INT8S dealProxyType3(PROXY_GETLIST *getlist,INT8U port485)
{
	INT8U addrlen=0;
	int dataindex=0;
	INT8S result = -1;
	INT8U mpindex = 0;
	INT16U singleLen = 0;
	INT8U tmpbuf[512]={};
	CLASS_6001 obj6001 = {};
	INT8U type = SET_REQUEST;
	fprintf(stderr,"\n\n\n -------------------port485 = %d dealProxyType3 代理对象(TSA)数量 objs num = %d :",port485,getlist->num);

	if((getlist->proxytype == ProxyActionRequestList)||(getlist->proxytype == ProxyActionThenGetRequestList))
	{
		type = ACTION_REQUEST;
	}
	INT8U prtIndex = 0,prtIndex1 = 0;

	fprintf(stderr,"TSA==getlist->num=%d=====\n",getlist->num);


	for(mpindex = 0;mpindex < getlist->num;mpindex++)
	{
		if((getlist->proxytype == ProxySetRequestList)||(getlist->proxytype == ProxyActionRequestList))
		{
			fprintf(stderr,"\n#####mpindex=%d,tsa=",mpindex);

			for(prtIndex = 0;prtIndex < 8;prtIndex++)
			{
				fprintf(stderr,"%02x ",getlist->proxy_obj.doTsaList[mpindex].tsa.addr[prtIndex]);
			}
			for(prtIndex = 0;prtIndex < getlist->proxy_obj.doTsaList[mpindex].num;prtIndex++)
			{
				fprintf(stderr,"\n\n SETOAD[%d] ",prtIndex);
				fprintf(stderr,"OAD = %04x%02x%02x",
						getlist->proxy_obj.doTsaList[mpindex].setobjs[prtIndex].oad.OI,
						getlist->proxy_obj.doTsaList[mpindex].setobjs[prtIndex].oad.attflg,
						getlist->proxy_obj.doTsaList[mpindex].setobjs[prtIndex].oad.attrindex);
				fprintf(stderr,"datatype = %02x ",getlist->proxy_obj.doTsaList[mpindex].setobjs[prtIndex].datatype);
				fprintf(stderr,"\n data[%d] = ",getlist->proxy_obj.doTsaList[mpindex].setobjs[prtIndex].len);
				for(prtIndex1 = 0;prtIndex1 < getlist->proxy_obj.doTsaList[mpindex].setobjs[prtIndex].len;prtIndex1++)
				{
					fprintf(stderr,"%02x ",getlist->proxy_obj.doTsaList[mpindex].setobjs[prtIndex].data[prtIndex1]);
				}
			}
		}
		if((getlist->proxytype == ProxySetThenGetRequestList)||(getlist->proxytype == ProxyActionThenGetRequestList))
		{
			fprintf(stderr," 1111111***********mpindex=%d\n",mpindex);
			printTSA(getlist->proxy_obj.doTsaThenGet[mpindex].tsa);

			fprintf(stderr,"\n#####mpindex=%d,oadnum = %d tsa= ",mpindex,getlist->proxy_obj.doTsaThenGet[mpindex].num);

			for(prtIndex = 0;prtIndex < 8;prtIndex++)
			{
				fprintf(stderr,"%02x ",getlist->proxy_obj.doTsaThenGet[mpindex].tsa.addr[prtIndex]);
			}
			for(prtIndex = 0;prtIndex < getlist->proxy_obj.doTsaThenGet[mpindex].num;prtIndex++)
			{
				fprintf(stderr,"\n\n SETOAD[%d] ",prtIndex);
				fprintf(stderr,"SETOAD = %04x%02x%02x",
						getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].oad_set.OI,
						getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].oad_set.attflg,
						getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].oad_set.attrindex);
				fprintf(stderr,"datatype = %02x ",getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].datatype);
				fprintf(stderr,"\n data[%d] = ",getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].len);
				for(prtIndex1 = 0;prtIndex1 < getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].len;prtIndex1++)
				{
					fprintf(stderr,"%02x ",getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].data[prtIndex1]);
				}
				fprintf(stderr,"GETOAD = %04x%02x%02x",
						getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].oad_get.OI,
						getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].oad_get.attflg,
						getlist->proxy_obj.doTsaThenGet[mpindex].setoads[prtIndex].oad_get.attrindex);
			}
		}

		if (proxyInUse.devUse.rs485Need == 0)
			break;
		fprintf(stderr," ***********mpindex=%d\n",mpindex);

		if((getlist->proxytype == ProxySetRequestList)||(getlist->proxytype == ProxyActionRequestList))
		{
			if( get6001ObjByTSA(getlist->proxy_obj.doTsaList[mpindex].tsa,&obj6001) != 1 ||
			    obj6001.basicinfo.port.attrindex != port485 ||
			    getComfdBy6001(obj6001.basicinfo.baud,obj6001.basicinfo.port.attrindex) != 1)
			{
				continue;
			}
		}
		if((getlist->proxytype == ProxySetThenGetRequestList)||(getlist->proxytype == ProxyActionThenGetRequestList))
		{
			if( get6001ObjByTSA(getlist->proxy_obj.doTsaThenGet[mpindex].tsa,&obj6001) != 1 ||
			    obj6001.basicinfo.port.attrindex != port485 ||
			    getComfdBy6001(obj6001.basicinfo.baud,obj6001.basicinfo.port.attrindex) != 1)
			{
				continue;
			}
		}
		fprintf(stderr,"\nRS485-%d 合法检查通过！ obj-%d ",port485,mpindex);
		INT8U portUse = obj6001.basicinfo.port.attrindex;
		switch(obj6001.basicinfo.protocol)
		{
			case DLT_645_07:
				//singleLen = dealProxy_645_07(getlist->proxy_obj.objs[index],tmpbuf,portUse,getlist->proxy_obj.objs[index].onetimeout);
				break;
			default:
				{
					if((getlist->proxytype == ProxySetRequestList)||(getlist->proxytype == ProxyActionRequestList))
					{
						singleLen = dealProxy_SET_ACTION_698(type,getlist->proxy_obj.doTsaList[mpindex],tmpbuf,portUse);
					}
					if((getlist->proxytype == ProxySetThenGetRequestList)||(getlist->proxytype == ProxyActionThenGetRequestList))
					{
						singleLen = dealProxy_SET_ACTION_THEN_GET698(type,getlist->proxy_obj.doTsaThenGet[mpindex],tmpbuf,portUse);
					}
				}

		}
		pthread_mutex_lock(&mutex);

		fprintf(stderr,"\nTSA 返回长度 singleLen=%d\n",singleLen);
		dataindex= getlist->datalen;
		if((getlist->proxytype == ProxySetRequestList)||(getlist->proxytype == ProxyActionRequestList))
		{
			addrlen = getlist->proxy_obj.doTsaList[mpindex].tsa.addr[0]+1;
			memcpy(&getlist->data[dataindex],&getlist->proxy_obj.doTsaList[mpindex].tsa.addr[0],addrlen);
			dataindex += addrlen;
			getlist->data[dataindex++] = getlist->proxy_obj.doTsaList[mpindex].num;
		}
		if((getlist->proxytype == ProxySetThenGetRequestList)||(getlist->proxytype == ProxyActionThenGetRequestList))
		{
			addrlen = getlist->proxy_obj.doTsaThenGet[mpindex].tsa.addr[0]+1;
			memcpy(&getlist->data[dataindex],&getlist->proxy_obj.doTsaThenGet[mpindex].tsa.addr[0],addrlen);
			dataindex += addrlen;
			getlist->data[dataindex++] = getlist->proxy_obj.doTsaThenGet[mpindex].num;
		}
		fprintf(stderr,"\nTSA addr长度 addrlen=%d\n",addrlen);

		fprintf(stderr,"\nTSA buf 指针 dataindex=%d\n",dataindex);

		if(singleLen > 0)
		{
			if((getlist->proxytype == ProxySetRequestList)||(getlist->proxytype == ProxyActionRequestList))
			{
				getlist->proxy_obj.doTsaList[mpindex].dar = proxy_success;
			}
			if((getlist->proxytype == ProxySetThenGetRequestList)||(getlist->proxytype == ProxyActionThenGetRequestList))
			{
				getlist->proxy_obj.doTsaThenGet[mpindex].dar = proxy_success;
			}
			fprintf(stderr,"\n $$$$$$$$$$$$$填充数据11111");
			for(prtIndex = 0;prtIndex<singleLen;prtIndex++)
			{
				fprintf(stderr,"%02x ",tmpbuf[prtIndex]);
			}
			memcpy(&getlist->data[dataindex],tmpbuf,singleLen);
			dataindex += singleLen;
			getlist->datalen += dataindex;
		}else {
			//只置DAR状态值，在组帧的时候proxy_dar_fill进行处理
			if((getlist->proxytype == ProxySetRequestList)||(getlist->proxytype == ProxyActionRequestList))
			{
				getlist->proxy_obj.doTsaList[mpindex].dar = request_overtime;
			}
			if((getlist->proxytype == ProxySetThenGetRequestList)||(getlist->proxytype == ProxyActionThenGetRequestList))
			{
				getlist->proxy_obj.doTsaThenGet[mpindex].dar = request_overtime;
			}
			fprintf(stderr,"\nProxySetRequestList overtime.....getlist->datalen=%d\n",getlist->datalen);
		}
		fprintf(stderr,"\n$$$$$$$$$$$$$填充数据11111");
		for(prtIndex = 0;prtIndex<getlist->datalen;prtIndex++)
		{
			fprintf(stderr,"%02x ",getlist->data[prtIndex]);
		}
//		else	//TODO：？？？没有数据放在最后统一处理
//		{
//			getlist->data[dataindex++] = 0;
//			getlist->datalen += dataindex;
//		}

		pthread_mutex_unlock(&mutex);
	}

	set_port_active(port485,0);
	return result;
}

INT8S dealProxy(PROXY_GETLIST *getlist,INT8U port485)
{
	INT8S result = -1;
	fprintf(stderr,"\nRS485 代理类型 =%d  datalen=%d",getlist->proxytype,getlist->proxylen);
	switch(getlist->proxytype) {
	case ProxyGetRequestList:
		result = dealProxyType1(getlist,port485);
		break;
	case ProxyTransCommandRequest:
		result = dealProxyType7(getlist,port485);
		break;
	case ProxyGetRequestRecord:
		result = dealProxyType2(getlist,port485);		//new
		break;
	case ProxySetRequestList:
	case ProxyActionRequestList:
	case ProxySetThenGetRequestList:
	case ProxyActionThenGetRequestList:
		result = dealProxyType3(getlist,port485);		//new
		break;
	}

	return result;
}

INT8S dealGuiRead(Proxy_Msg pMsg,INT8U port485)
{
	INT8S result = -1;
	if(pMsg.port.attrindex != port485)
	{
		fprintf(stderr,"dealGuiRead 非本端口测量点不处理");
		return result;
	}
	DbgPrintToFile1(port485,"\n dealGuiRead 处理液晶点抄 :%02x%02x%02x%02x%02x%02x%02x%02x 波特率=%d protocol=%d 端口号=%04x%02x%02x 规约类型=%d 数据标识=%04x"
			,pMsg.addr.addr[0],pMsg.addr.addr[1],pMsg.addr.addr[2],pMsg.addr.addr[3]
			,pMsg.addr.addr[4],pMsg.addr.addr[5],pMsg.addr.addr[6],pMsg.addr.addr[7]
			,pMsg.baud,pMsg.protocol,pMsg.port.OI,pMsg.port.attflg,pMsg.port.attrindex
			,pMsg.protocol,pMsg.oi);

	if(getComfdBy6001(pMsg.baud,pMsg.port.attrindex) != 1)
	{
		p_Proxy_Msg_Data->done_flag = 1;
		fprintf(stderr,"\ndealGuiRead 参数错误");
		return result;
	}
	CLASS_6001 meter = {};
	CLASS_6035 st6035;
	INT16S retLen = -1;
	INT8U dataContent[DATA_CONTENT_LEN];
	memset(dataContent,0,DATA_CONTENT_LEN);
	memcpy(meter.basicinfo.addr.addr,pMsg.addr.addr,TSA_LEN);
	meter.basicinfo.baud = pMsg.baud;
	meter.basicinfo.port.OI = pMsg.port.OI;
	meter.basicinfo.port.attflg = pMsg.port.attflg;
	meter.basicinfo.port.attrindex = pMsg.port.attrindex;
	meter.basicinfo.protocol = pMsg.protocol;
	switch(meter.basicinfo.protocol)
	{
	case DLT_645_97:
			{
				OAD requestOAD;
				requestOAD.OI = pMsg.oi;
				requestOAD.attflg = 0x02;
				requestOAD.attrindex = 0x00;

				C601F_645 Flag645;
				memset(&Flag645,0,sizeof(C601F_645));
				Flag645.protocol = DLT_645_97;

				if(OADMap07DI(0x0000,requestOAD, &Flag645) == 1)
				{
					retLen = request698_97Data(Flag645.DI._97.DI_1[0],dataContent,meter,&st6035,meter.basicinfo.port.attrindex);
				}
			}
			break;
		case DLT_645_07:
			{
				OAD requestOAD;
				requestOAD.OI = pMsg.oi;
				requestOAD.attflg = 0x02;
				requestOAD.attrindex = 0x00;

				C601F_645 Flag645;
				memset(&Flag645,0,sizeof(C601F_645));
				Flag645.protocol = DLT_645_07;

				if(OADMap07DI(0x0000,requestOAD, &Flag645) == 1)
				{
					retLen = request698_07Data(Flag645.DI._07.DI_1[0],dataContent,meter,&st6035,meter.basicinfo.port.attrindex);
				}
			}
			break;
		default:
			{

				CLASS_6015 st6015;
				memset(&st6015,0,sizeof(CLASS_6015));
				st6015.cjtype = TYPE_NULL;
				st6015.csds.num = 1;
				st6015.csds.csd[0].type = 0;
				st6015.csds.csd[0].csd.oad.OI = pMsg.oi;
				st6015.csds.csd[0].csd.oad.attflg = 0x02;
				st6015.csds.csd[0].csd.oad.attrindex = 0x00;
				retLen = request698_singleOAD(st6015,meter,dataContent, meter.basicinfo.port.attrindex);
			}

	}
	DbPrt1(meter.basicinfo.port.attrindex,"点抄回复数据:", (char *) dataContent, 27, NULL);
	if(retLen > 0)
	{
		TSGet(&p_Proxy_Msg_Data->realdata.tm_collect);
		memcpy(p_Proxy_Msg_Data->realdata.data_All,&dataContent[3],4);
		memcpy(p_Proxy_Msg_Data->realdata.Rate1_Data,&dataContent[8],4);
		memcpy(p_Proxy_Msg_Data->realdata.Rate2_Data,&dataContent[13],4);
		memcpy(p_Proxy_Msg_Data->realdata.Rate3_Data,&dataContent[18],4);
		memcpy(p_Proxy_Msg_Data->realdata.Rate4_Data,&dataContent[23],4);

	}
	else
	{
		memset(&p_Proxy_Msg_Data->realdata,0xee,sizeof(RealDataInfo));
	}

	p_Proxy_Msg_Data->done_flag = 1;
	fprintf(stderr,"\n点抄返回******************\n");
	return result;
}

INT8S readMeterPowerInfo()
{
	fprintf(stderr,"\n\n上电抄读停上电事件 readMeterPowerInfo");
	DbgPrintToFile1(1,"\n **************************上电抄读停上电事件 readMeterPowerInfo");
	INT8S result = -1;

	INT8U meterIndex;

	for(meterIndex = 0; meterIndex < POWEROFFON_NUM; meterIndex++)
	{
#if 0
		fprintf(stderr,"\nMeterPowerInfo[%d] ARRD = %02x%02x%02x%02x%02x%02x%02x%02x  ERC3106State = %d \n",meterIndex,
				MeterPowerInfo[meterIndex].tsa.addr[0],MeterPowerInfo[meterIndex].tsa.addr[1],MeterPowerInfo[meterIndex].tsa.addr[2],
				MeterPowerInfo[meterIndex].tsa.addr[3],MeterPowerInfo[meterIndex].tsa.addr[4],MeterPowerInfo[meterIndex].tsa.addr[5],
				MeterPowerInfo[meterIndex].tsa.addr[6],MeterPowerInfo[meterIndex].tsa.addr[7],
				MeterPowerInfo[meterIndex].ERC3106State);
#endif

		if(MeterPowerInfo[meterIndex].ERC3106State==1)
		{
#if 0
			DbgPrintToFile1(1,"\nMeterPowerInfo[%d] ARRD = %02x%02x%02x%02x%02x%02x%02x%02x  ERC3106State = %d \n",meterIndex,
					MeterPowerInfo[meterIndex].tsa.addr[0],MeterPowerInfo[meterIndex].tsa.addr[1],MeterPowerInfo[meterIndex].tsa.addr[2],
					MeterPowerInfo[meterIndex].tsa.addr[3],MeterPowerInfo[meterIndex].tsa.addr[4],MeterPowerInfo[meterIndex].tsa.addr[5],
					MeterPowerInfo[meterIndex].tsa.addr[6],MeterPowerInfo[meterIndex].tsa.addr[7],
					MeterPowerInfo[meterIndex].ERC3106State);
#endif
			CLASS_6001 obj6001;
			//通过表地址找 6001
			if(get6001ObjByTSA(MeterPowerInfo[meterIndex].tsa,&obj6001) != 1 )
			{
				fprintf(stderr," readMeterPowerInfo-------1 未找到相应6001");
				DbgPrintToFile1(1," readMeterPowerInfo-------1 未找到相应6001");
				continue;
			}


			if(getComfdBy6001(obj6001.basicinfo.baud,obj6001.basicinfo.port.attrindex) != 1)
			{
				fprintf(stderr," readMeterPowerInfo--------2");
				DbgPrintToFile1(1," readMeterPowerInfo--------2");
				continue;
			}
			INT8U port485 = obj6001.basicinfo.port.attrindex;

			CLASS_6035 invalidst6035;
			INT8U dataContent[BUFFSIZE128];
			memset(dataContent,0,BUFFSIZE128);

			switch(obj6001.basicinfo.protocol)
			{
			case DLT_645_07:
				{
					 	request698_07Data(flag07_tingshangdian,dataContent,obj6001,&invalidst6035,port485);

						MeterPowerInfo[meterIndex].PoweroffTime.tm_year = dataContent[1];
						MeterPowerInfo[meterIndex].PoweroffTime.tm_year = (MeterPowerInfo[meterIndex].PoweroffTime.tm_year<<8);
						MeterPowerInfo[meterIndex].PoweroffTime.tm_year = MeterPowerInfo[meterIndex].PoweroffTime.tm_year + dataContent[2] - 1900;
						MeterPowerInfo[meterIndex].PoweroffTime.tm_mon = dataContent[3] -1;
						MeterPowerInfo[meterIndex].PoweroffTime.tm_mday = dataContent[4];
						MeterPowerInfo[meterIndex].PoweroffTime.tm_hour = dataContent[5];
						MeterPowerInfo[meterIndex].PoweroffTime.tm_min = dataContent[6];
						MeterPowerInfo[meterIndex].PoweroffTime.tm_sec = dataContent[7];

						MeterPowerInfo[meterIndex].PoweronTime.tm_year = dataContent[9];
						MeterPowerInfo[meterIndex].PoweronTime.tm_year = (MeterPowerInfo[meterIndex].PoweronTime.tm_year<<8);
						MeterPowerInfo[meterIndex].PoweronTime.tm_year = MeterPowerInfo[meterIndex].PoweronTime.tm_year +  dataContent[10] - 1900;

						MeterPowerInfo[meterIndex].PoweronTime.tm_mon =  dataContent[11] -1;
						MeterPowerInfo[meterIndex].PoweronTime.tm_mday =  dataContent[12];
						MeterPowerInfo[meterIndex].PoweronTime.tm_hour =  dataContent[13];
						MeterPowerInfo[meterIndex].PoweronTime.tm_min =  dataContent[14];
						MeterPowerInfo[meterIndex].PoweronTime.tm_sec =  dataContent[15];

						MeterPowerInfo[meterIndex].Valid = 1;
						DbPrt1(port485,"07表停上电:", (char *) dataContent, 16, NULL);
				}
				break;
			default:
				{
					CLASS_6015 st6015;
					memset(&st6015,0,sizeof(CLASS_6015));
					st6015.cjtype = TYPE_NULL;
					st6015.csds.num = 1;
					st6015.csds.csd[0].type = 0;
					st6015.csds.csd[0].csd.oad.OI = 0x3011;
					st6015.csds.csd[0].csd.oad.attflg = 02;
					st6015.csds.csd[0].csd.oad.attrindex = 01;
					INT16S sendLen = 0;
					INT16S recvLen = 0;

					INT8U sendbuff[BUFFSIZE128];
					INT8U recvbuff[BUFFSIZE2048];

					memset(sendbuff, 0, BUFFSIZE128);

					sendLen = composeProtocol698_GetRequest(sendbuff, st6015,obj6001.basicinfo.addr);
					if(sendLen < 0)
					{
						fprintf(stderr,"deal6015_698  sendLen < 0");
						continue;
					}

					INT8U subindex = 0;
					while(subindex < MAX_RETRY_NUM)
					{
						memset(recvbuff, 0, BUFFSIZE2048);
						SendDataTo485(port485, sendbuff, sendLen);
						recvLen = ReceDataFrom485(DLT_698,port485, 500, recvbuff);
						fprintf(stderr,"\n\n recvLen = %d \n",recvLen);
						if(recvLen > 0)
						{

							INT8U csdNum = 0;
							INT16S dataLen = recvLen;
							INT8U apduDataStartIndex = 0;
							INT8U getResponseType = analyzeProtocol698(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
							fprintf(stderr,"\n getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);

							if(getResponseType > 0)
							{
								if(recvbuff[apduDataStartIndex+4] == 1)
								{
									fprintf(stderr,"\n readMeterPowerInfo datacontent = ");
//									INT8U prtIndex = 0;

//									for(prtIndex = (apduDataStartIndex+13);prtIndex < (apduDataStartIndex+27);prtIndex++)
//									{
//										fprintf(stderr,"%02x ",recvbuff[prtIndex]);
//									}
									MeterPowerInfo[meterIndex].PoweroffTime.tm_year = recvbuff[apduDataStartIndex+13];
									MeterPowerInfo[meterIndex].PoweroffTime.tm_year = (MeterPowerInfo[meterIndex].PoweroffTime.tm_year<<8);
									MeterPowerInfo[meterIndex].PoweroffTime.tm_year = MeterPowerInfo[meterIndex].PoweroffTime.tm_year + recvbuff[apduDataStartIndex+14] - 1900;
									MeterPowerInfo[meterIndex].PoweroffTime.tm_mon = recvbuff[apduDataStartIndex+15] -1;
									MeterPowerInfo[meterIndex].PoweroffTime.tm_mday = recvbuff[apduDataStartIndex+16];
									MeterPowerInfo[meterIndex].PoweroffTime.tm_hour = recvbuff[apduDataStartIndex+17];
									MeterPowerInfo[meterIndex].PoweroffTime.tm_min = recvbuff[apduDataStartIndex+18];
									MeterPowerInfo[meterIndex].PoweroffTime.tm_sec = recvbuff[apduDataStartIndex+19];

									MeterPowerInfo[meterIndex].PoweronTime.tm_year = recvbuff[apduDataStartIndex+21];
									MeterPowerInfo[meterIndex].PoweronTime.tm_year = (MeterPowerInfo[meterIndex].PoweronTime.tm_year<<8);
									MeterPowerInfo[meterIndex].PoweronTime.tm_year = MeterPowerInfo[meterIndex].PoweronTime.tm_year + recvbuff[apduDataStartIndex+22] - 1900;

									MeterPowerInfo[meterIndex].PoweronTime.tm_mon =  recvbuff[apduDataStartIndex+23] -1;
									MeterPowerInfo[meterIndex].PoweronTime.tm_mday = recvbuff[apduDataStartIndex+24];
									MeterPowerInfo[meterIndex].PoweronTime.tm_hour = recvbuff[apduDataStartIndex+25];
									MeterPowerInfo[meterIndex].PoweronTime.tm_min = recvbuff[apduDataStartIndex+26];
									MeterPowerInfo[meterIndex].PoweronTime.tm_sec = recvbuff[apduDataStartIndex+27];

									MeterPowerInfo[meterIndex].Valid = 1;
									DbPrt1(port485,"698表停上电:", (char *) &recvbuff[apduDataStartIndex+12], 16, NULL);

								}
								break;
							}

						}
						subindex++;
					}
				}

			}
		}
	}
	DbgPrintToFile1(1,"\n 上电抄读停上电事件 readMeterPowerInfo**************************");
	return result;
}
INT8S dealProxyQueue(INT8U port485)
{
	INT8S result = 0;

	if(cjcommProxy.isInUse&(1<<(port485-1)))
	{
		fprintf(stderr,"\n-------------------------485 %d 开始代理工作 测量点数量=%d\n",port485,cjcommProxy.strProxyList.num);
		result = dealProxy(&cjcommProxy.strProxyList,port485);
		cjcommProxy.isInUse &= ~(1<<(port485-1));
		fprintf(stderr,"\n-------------------------result=%d,strProxyList.datalen=%d\n",result,cjcommProxy.strProxyList.datalen);
	}
	if(cjguiProxy.isInUse&(1<<(port485-1)))
	{
		result = dealGuiRead(cjguiProxy.strProxyMsg,port485);
		cjguiProxy.isInUse &= ~(1<<(port485-1));
	}

	return result;
}
INT8S sendBroadCastTime07(INT8U port485)
{
	INT8S ret = -1;
	INT8U sendBuf[BUFFSIZE128];
	memset(sendBuf, 0, BUFFSIZE128);

	TS nowTime;
	TSGet(&nowTime);

	FORMAT07 format07_setTime;
	format07_setTime.Ctrl = 0x08;

	format07_setTime.Time[0] = nowTime.Sec;
	format07_setTime.Time[1] = nowTime.Minute;
	format07_setTime.Time[2] = nowTime.Hour;
	format07_setTime.Time[3] = nowTime.Day;
	format07_setTime.Time[4] = nowTime.Month;
	format07_setTime.Time[5] = nowTime.Year-2000;

	memset(&format07_setTime.Addr, 0x99, 6);//地址

	INT16S SendLen = composeProtocol07(&format07_setTime, sendBuf);
	if (SendLen < 0)
	{
		fprintf(stderr, "sendBroadCastTime07 error ");
		return ret;
	}
	SendDataTo485(port485, sendBuf, SendLen);
	return ret;
}
INT8S sendSetTimeCMD698(TSA meterAddr,INT8U port485,INT8U isbroadCast)
{
	INT8S ret = -1;
	INT16S sendLen;
	INT8U sendBuf[BUFFSIZE128];
	INT8U recvBuf[BUFFSIZE2048];
	memset(sendBuf, 0, BUFFSIZE128);
	memset(recvBuf, 0, BUFFSIZE2048);
	INT8U subindex = 0;
	INT16S RecvLen = 0;
	fprintf(stderr,"\n 下发对时报文");
	//下发对时
	ACTION_SET_OBJ setOBJ;
	memcpy(&setOBJ.tsa,&meterAddr,sizeof(TSA));
	setOBJ.num = 1;
	setOBJ.setobjs[0].oad.OI = 0x4000;
	setOBJ.setobjs[0].oad.attflg = 0x02;
	setOBJ.setobjs[0].oad.attrindex = 0x00;
	DateTimeBCD nowtime;
	DataTimeGet(&nowtime);

	INT8U timeData[8];
	setOBJ.setobjs[0].len = fill_date_time_s(timeData,&nowtime);

	memcpy(setOBJ.setobjs[0].data,timeData,8);
	if(isbroadCast)
	{
		INT8U length = (meterAddr.addr[1]&0x0f) + 1;//sizeof(addr)-1;//服务器地址长度
		memset(&meterAddr.addr[2],0xAA,length);
		meterAddr.addr[1] = meterAddr.addr[1] | 0xc0;//广播地址
	}

	sendLen = composeProtocol698_SetActionRequest(sendBuf,SET_REQUEST,setOBJ);
	fprintf(stderr,"sendSetTimeCMD sendLen698 = %d",sendLen);
	DbPrt1(port485,"698 下发对时报文:", (char *) sendBuf, sendLen, NULL);
	subindex = 0;
	while(subindex < MAX_RETRY_NUM)
	{
		SendDataTo485(port485, sendBuf, sendLen);
		RecvLen = ReceDataFrom485(DLT_698,port485, 500, recvBuf);

		fprintf(stderr,"\n\n recvLen = %d \n",RecvLen);
		if(RecvLen > 0)
		{
			return ret;
		}
		subindex++;
	}


	return ret;
}
INT8S requestMeterTime(INT8U port485,CLASS_6001 meter,INT8U* dataContent)
{
	INT8S ret = -1;
	CLASS_6035 nullst6035;
	memset(&nullst6035,0,sizeof(CLASS_6035));
	INT16S dataLen = 0;
	switch(meter.basicinfo.protocol)
	{
		case DLT_645_07:
		{
			OAD timeOAD;
			timeOAD.OI = 0x4000;
			timeOAD.attflg = 0x02;
			timeOAD.attrindex = 0x00;
			dataLen = request9707_singleOAD(meter.basicinfo.protocol,0x0000,timeOAD,meter,&nullst6035,dataContent,port485);
		}
		break;
		default:
		{
			CLASS_6015 st6015;
			memset(&st6015,0,sizeof(CLASS_6015));

			st6015.cjtype = TYPE_NULL;
			st6015.csds.num = 1;
			st6015.csds.csd[0].type = 0;
			st6015.csds.csd[0].csd.oad.OI = 0x4000;
			st6015.csds.csd[0].csd.oad.attflg = 0x02;
			st6015.csds.csd[0].csd.oad.attrindex = 0x00;
			dataLen = request698_singleOAD(st6015,meter,dataContent, port485);
		}
	}
	return ret;
}
INT8S dealBroadCastSingleMeter(INT8U port485,CLASS_6001 meter)
{
	fprintf(stderr,"终端单地址广播校时 测量点序号 = %d",meter.sernum);

	INT8S ret = 0;

	if(getComfdBy6001(meter.basicinfo.baud,meter.basicinfo.port.attrindex) != 1)
	{
		fprintf(stderr," dealBroadCastSingleMeter--------2");
		return ret;
	}

	INT8U dataContent[DATA_CONTENT_LEN];
	memset(dataContent,0,DATA_CONTENT_LEN);

	requestMeterTime(port485,meter,dataContent);
	if(dataContent[0]!=0x1c)
	{
		return ret;
	}
	time_t timeNow = time(NULL);
	TS meterTime;
	meterTime.Year = dataContent[1];
	meterTime.Year = (meterTime.Year << 8) + dataContent[2];
	meterTime.Month = dataContent[3];
	meterTime.Day = dataContent[4];
	meterTime.Hour = dataContent[5];
	meterTime.Minute = dataContent[6];
	meterTime.Sec = dataContent[7];

	int time_offset=difftime(timeNow,tmtotime_t(meterTime));
	DbgPrintToFile1(port485,"before settiem 电表[%d]时间差:%d",meter.sernum,time_offset);
	INT8U eventbuf[8] = {0};

	memcpy(eventbuf,&dataContent[1],7);
	eventbuf[7] = (INT8U)time_offset;

	if(abs(time_offset) > broadcase4204.upleve)
	{
		if(meter.basicinfo.protocol == DLT_645_07)
		{
			DbgPrintToFile1(port485,"\n 07表对时后仍然超时 对时事件buff = %02x%02x%02x%02x%02x%02x%02x 时间差=%d \n",
								eventbuf[0],eventbuf[1],eventbuf[2],eventbuf[3],eventbuf[4],eventbuf[5],eventbuf[6],eventbuf[7]);

			Event_311B(meter.basicinfo.addr,eventbuf,8,JProgramInfo);
		}
		else
		{
			sendSetTimeCMD698(meter.basicinfo.addr,port485,0);
			sleep(5);

			memset(dataContent,0,DATA_CONTENT_LEN);
			requestMeterTime(port485,meter,dataContent);
			if(dataContent[0]!=0x1c)
			{
				time_offset = 1;
			}
			else
			{
				meterTime.Year = dataContent[1];
				meterTime.Year = (meterTime.Year << 8) + dataContent[2];
				meterTime.Month = dataContent[3];
				meterTime.Day = dataContent[4];
				meterTime.Hour = dataContent[5];
				meterTime.Minute = dataContent[6];
				meterTime.Sec = dataContent[7];
				timeNow = time(NULL);
				time_offset=difftime(timeNow,tmtotime_t(meterTime));
			}
			eventbuf[7] = (INT8U)time_offset;
			if(abs(time_offset)  < broadcase4204.upleve)
			{
				DbgPrintToFile1(port485,"\n698 对时事件buff = %02x%02x%02x%02x%02x%02x%02x 时间差=%d \n",
						eventbuf[0],eventbuf[1],eventbuf[2],eventbuf[3],eventbuf[4],eventbuf[5],eventbuf[6],eventbuf[7]);

				Event_311B(meter.basicinfo.addr,eventbuf,8,JProgramInfo);
			}
		}

	}

	return ret;
}
/*
 * 处理广播校时
 * */
INT8S checkBroadCast(INT8U port485)
{
	INT8S 	ret = -1;
	static  INT8U broadtime[5],broad_day=0;
	TS nowTime;

//	fprintf(stderr,"port485=%d enable=%d broadtime=%d-%d-%d-%d\n",port485,broadcase4204.enable,broadtime[0],broadtime[1],broadtime[2],broadtime[3]);
	if(port485 > S4853) {
		asyslog(LOG_WARNING,"广播对时串口%d 大于集中器串口容量%d 无效退出",port485,S4853);
		return ret;
	}

	if(broadcase4204.enable) {		//广播对时有效，每天清除对时标记，保证到启动时间，进入一次
		TSGet(&nowTime);
		if(broad_day != nowTime.Day) {
			broad_day = nowTime.Day;
			memset(&broadtime,0,sizeof(broadtime));
		}
	}
	if(broadcase4204.enable && broadtime[port485]==0)	//增加标记，到时间只下发一次广播对时命令,防止单地址广播校时未到时间重复进入
	{
		TSGet(&nowTime);
		TS broadcastTime;
		broadcastTime.Year = nowTime.Year;
		broadcastTime.Month = nowTime.Month;
		broadcastTime.Day = nowTime.Day;
		broadcastTime.Hour = broadcase4204.startime[0];//broadcase4204.startime1[0];
		broadcastTime.Minute = broadcase4204.startime[1];
		broadcastTime.Sec = broadcase4204.startime[2];

		INT8U timeCmp = TScompare(nowTime,broadcastTime);
		fprintf(stderr,"now Hour=%d, %d  timeCmp=%d\n",nowTime.Hour,broadcastTime.Hour,timeCmp);
		if(timeCmp < 2)
		{
			asyslog(LOG_WARNING,"抄表口[%d],广播校时时间到",port485); //将下面几句话移到timeCmp<2之内，到广播对时时间再进行对时

			INT8U port = port485-1;
			INT16U meterIndex = 0;
			CLASS_6001 meter = { };
			INT8U isHas698Meter = 0;
			INT8U isHas07Meter = 0;
			for (meterIndex = 0; meterIndex < info6000[port].meterSum; meterIndex++)
			{
				dealProxyQueue(port485);
				memset(&meter,0,sizeof(CLASS_6001));
				if (readParaClass(0x6000, &meter, info6000[port].list6001[meterIndex]) == 1)
				{
					if (meter.sernum != 0 && meter.sernum != 0xffff)
					{
						if((is485OAD(meter.basicinfo.port,port485)==1) && (meter.basicinfo.protocol == DLT_698)&& (isHas698Meter == 0))
						{
							TSA meterAddr;
							meterAddr.addr[0] = 7;
							meterAddr.addr[1] = 5;
							sendSetTimeCMD698(meterAddr,port485,1);	//？？？？698广播校时时间不正确
							isHas698Meter = 1;
						}
						if((is485OAD(meter.basicinfo.port,port485)==1) && (meter.basicinfo.protocol == DLT_645_07)&& (isHas07Meter == 0))
						{
							sendBroadCastTime07(port485);
							isHas07Meter = 1;
						}
						if((isHas698Meter==1)&&(isHas07Meter==1))
						{
							break;
						}
					}

				}
			}

			broadtime[port485] = 1;
		}
	}
	if(broadcase4204.enable1)
	{
		TSGet(&nowTime);
		TS broadcastTime;
		broadcastTime.Year = nowTime.Year;
		broadcastTime.Month = nowTime.Month;
		broadcastTime.Day = nowTime.Day;
		broadcastTime.Hour = broadcase4204.startime1[0];
		broadcastTime.Minute = broadcase4204.startime1[1];
		broadcastTime.Sec = broadcase4204.startime1[2];

		INT8U timeCmp = TScompare(nowTime,broadcastTime);
		//fprintf(stderr,"\n checkBroadCast timeCmp = %d",timeCmp);
		if(timeCmp < 2)
		{
			asyslog(LOG_WARNING,"终端单地址广播校时时间到");
			INT8U port = port485-1;
			INT16U meterIndex = 0;
			CLASS_6001 meter = { };

			sleep(5);
			INT8U isHas07Meter = 0;
			for (meterIndex = 0; meterIndex < info6000[port].meterSum; meterIndex++)
			{
				dealProxyQueue(port485);
				memset(&meter,0,sizeof(CLASS_6001));
				if (readParaClass(0x6000, &meter, info6000[port].list6001[meterIndex]) == 1)
				{
					if (meter.sernum != 0 && meter.sernum != 0xffff)
					{
						if((is485OAD(meter.basicinfo.port,port485)==1) && (meter.basicinfo.protocol == DLT_698))
						{
							dealBroadCastSingleMeter(port485,meter);
						}
					}
					if((is485OAD(meter.basicinfo.port,port485)==1) && (meter.basicinfo.protocol == DLT_645_07))
					{
						isHas07Meter = 1;
					}
				}
			}
			if(isHas07Meter == 0)
			{
				flagDay_4204[port485-1] = 0;
				return ret;
			}
			//07表广播对时
			sendBroadCastTime07(port485);
			sleep(5);

			for (meterIndex = 0; meterIndex < info6000[port].meterSum; meterIndex++)
			{
				dealProxyQueue(port485);
				memset(&meter,0,sizeof(CLASS_6001));
				if (readParaClass(0x6000, &meter, info6000[port].list6001[meterIndex]) == 1)
				{
					if (meter.sernum != 0 && meter.sernum != 0xffff)
					{
						if((is485OAD(meter.basicinfo.port,port485)==1) && (meter.basicinfo.protocol == DLT_645_07))
						{
							dealBroadCastSingleMeter(port485,meter);
						}
					}
				}
			}
			flagDay_4204[port485-1] = 0;
		}
	}

	return ret;
}
void sendProxyFault(PROXY_GETLIST getlist)
{
	OAD	oad={};
	getlist.status = 3;
	getlist.datalen = 1;
	memset(getlist.data,0,BUFFSIZE512);
	mqs_send((INT8S *)PROXY_NET_MQ_NAME,1,TERMINALPROXY_RESPONSE,oad,(INT8U *)&getlist,sizeof(PROXY_GETLIST));
	fprintf(stderr,"\n代理消息已经发出\n\n");
}
//处理代理抄读停上实时请求-
INT8S dealRealTimeRequst(INT8U port485)
{
	if((port485==S4852)&&(isNeed4852 ==0))
		return 0;

	INT8S result = 0;
	while(readState)
	{
		DbgPrintToFile1(port485,"\n 另一个线程正在处理消息 dealRealTimeRequst \n");
		sleep(1);
	}
	//处理代理
	result = dealProxyQueue(port485);
	//fprintf(stderr,"\n poweroffon_state = %d",poweroffon_state);
	//先抄读停上电事件
	if(poweroffon_state == 1)
	{
		readState = 1;
		DbgPrintToFile1(port485,"poweroffon_state=%d!!!---------------",poweroffon_state);
		readMeterPowerInfo();
		poweroffon_state =2;
		readState = 0;
	}

	if(flagDay_4204[port485-1] == 1)
	{
		checkBroadCast(port485);
	}
	//fprintf(stderr,"port485 = %d para_change485 = %d",port485,para_change485[port485-1]);
	if(para_change485[port485-1] == 1)
	{
		result = PARA_CHANGE_RETVALUE;
	}

	return result;
}
INT16S request698_singleOAD(CLASS_6015 st6015, CLASS_6001 to6001,INT8U* data,INT8U port485)
{
	INT16S recvLen = 0,dataLen = 0,sendLen = 0;
	INT8U getResponseType = 0,csdNum = 0;
	INT8U sendbuff[BUFFSIZE128];
	INT8U recvbuff[BUFFSIZE2048];
	memset(sendbuff, 0, BUFFSIZE128);
	memset(recvbuff, 0, BUFFSIZE2048);
	sendLen = composeProtocol698_GetRequest(sendbuff, st6015,to6001.basicinfo.addr);
	if(sendLen < 0)
	{
		//fprintf(stderr,"deal6015_698  sendLen < 0");
		return dataLen;
	}

	SendDataTo485(port485, sendbuff, sendLen);
	recvLen = ReceDataFrom485(DLT_698,port485, 500, recvbuff);

	if(recvLen > 0)
	{
		dataLen = recvLen;
		INT8U apduDataStartIndex = 0;
		getResponseType = analyzeProtocol698(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
		//fprintf(stderr,"\n getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);
		if(getResponseType > 0)
		{
#ifdef TESTDEF1
			fprintf(stderr,"request698_singleOAD Buf[%d] = \n",dataLen);
			INT16U prtIndex =0;
			for(prtIndex = 0;prtIndex < dataLen;prtIndex++)
			{
				fprintf(stderr,"%02x ",recvbuff[apduDataStartIndex+prtIndex]);
				if((prtIndex+1)%20 ==0)
				{
					fprintf(stderr,"\n");
				}
			}
#endif

			apduDataStartIndex += 4;

			if(recvbuff[apduDataStartIndex++] == 0)//0------ 没有数据  1--数据
			{
				return dataLen;
			}
			else
			{
				INT16U startIndex = 0;
				startIndex = apduDataStartIndex;
				dataLen = getSinglegOADDataUnit(&recvbuff[apduDataStartIndex]);
				apduDataStartIndex += dataLen;
				memcpy(data,&recvbuff[startIndex],dataLen);
			}
		}
	}
	return dataLen;
}
//处理曲线数据
INT16S dealCurve_698(CLASS_6015 st6015, CLASS_6001 to6001,CLASS_6035* st6035,INT8U port485)
{
	INT16S sendLen = 0,recvLen = 0,readTimes = 0,dataCount = 0;
	INT8U readTimeIndex = 0,tsIndex = 0,dataNumOneGroup = 4;//一次抄4个数据点
	TS ts_start;
	ts_start.Year = (st6015.data.data[CURVE_INFO_STARTINDEX+16]<<8) + st6015.data.data[CURVE_INFO_STARTINDEX+17];
	ts_start.Month = st6015.data.data[CURVE_INFO_STARTINDEX+18];
	ts_start.Day = st6015.data.data[CURVE_INFO_STARTINDEX+19];
	ts_start.Hour = st6015.data.data[CURVE_INFO_STARTINDEX+20];
	ts_start.Minute = st6015.data.data[CURVE_INFO_STARTINDEX+21];
	INT16U dataNum = (st6015.data.data[CURVE_INFO_STARTINDEX+22]<<8) + st6015.data.data[CURVE_INFO_STARTINDEX+23];

	if(dataNum >= dataNumOneGroup)
	{
		readTimes = dataNum/dataNumOneGroup;
	}
	else
	{
		readTimes = dataNum;
		dataNumOneGroup = 1;
	}
	DbgPrintToFile1(port485,"dataNum = %d dataNumOneGroup = %d readTimes = %d",dataNum,dataNumOneGroup,readTimes);
	//一次抄读四个数据点
	for(readTimeIndex=0;readTimeIndex<readTimes;readTimeIndex++)
	{
		INT16S ret = 0;
		ret = dealRealTimeRequst(port485);
		if(ret == PARA_CHANGE_RETVALUE)
		{
			return PARA_CHANGE_RETVALUE;
		}

		INT16U tmpTime = ts_start.Year;
		st6015.data.data[CURVE_INFO_STARTINDEX+8] = 0x1c;
		st6015.data.data[CURVE_INFO_STARTINDEX+9] = (tmpTime>>8)&0x00ff;
		st6015.data.data[CURVE_INFO_STARTINDEX+10] = tmpTime&0x00ff;
		st6015.data.data[CURVE_INFO_STARTINDEX+11] = ts_start.Month;
		st6015.data.data[CURVE_INFO_STARTINDEX+12] = ts_start.Day;
		st6015.data.data[CURVE_INFO_STARTINDEX+13] = ts_start.Hour;
		st6015.data.data[CURVE_INFO_STARTINDEX+14] = ts_start.Minute;
		st6015.data.data[CURVE_INFO_STARTINDEX+15] = 0;

		for(tsIndex = 0;tsIndex < dataNumOneGroup;tsIndex++)
		{
			INT32S bactm = 0-((st6015.data.data[1]<<8)+st6015.data.data[2]);
			tminc(&ts_start,st6015.data.data[0],bactm);
		}

		DbgPrintToFile1(port485,"第[%d]次抄读曲线-----------------开始时标 %04d-%02d-%02d %02d:%02d:%02d \n",
				readTimeIndex,ts_start.Year,ts_start.Month,ts_start.Day,ts_start.Hour,ts_start.Minute,ts_start.Sec);
		tmpTime = ts_start.Year;
		st6015.data.data[CURVE_INFO_STARTINDEX] = 0x1c;
		st6015.data.data[CURVE_INFO_STARTINDEX+1] = (tmpTime>>8)&0x00ff;
		st6015.data.data[CURVE_INFO_STARTINDEX+2] = tmpTime&0x00ff;
		st6015.data.data[CURVE_INFO_STARTINDEX+3] = ts_start.Month;
		st6015.data.data[CURVE_INFO_STARTINDEX+4] = ts_start.Day;
		st6015.data.data[CURVE_INFO_STARTINDEX+5] = ts_start.Hour;
		st6015.data.data[CURVE_INFO_STARTINDEX+6] = ts_start.Minute;
		st6015.data.data[CURVE_INFO_STARTINDEX+7] = 0;
		INT8U curvedataContent[BUFFSIZE2048];
		memset(curvedataContent,0,BUFFSIZE2048);

		INT8U sendbuff[BUFFSIZE512];
		INT8U recvbuff[BUFFSIZE2048];

		memset(sendbuff, 0, BUFFSIZE512);
		memset(recvbuff, 0, BUFFSIZE2048);

		sendLen = composeProtocol698_GetRequest_RN(sendbuff, st6015,to6001.basicinfo.addr);
		if(sendLen < 0)
		{
			fprintf(stderr,"deal6015_698  sendLen < 0");
			return dataCount;
		}
		SendDataTo485(port485, sendbuff, sendLen);
		st6035->sendMsgNum++;

		recvLen = ReceDataFrom485(DLT_698,port485, 2000, recvbuff);

		fprintf(stderr,"\n\n recvLen = %d \n",recvLen);
		if(recvLen > 0)
		{
			st6035->rcvMsgNum++;
			INT8U csdNum = 0;
			INT16S dataLen = recvLen;
			INT8U apduDataStartIndex = 0;
			INT8U getResponseType = analyzeProtocol698_RN(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
			DbgPrintToFile1(port485,"\n getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);
			if(getResponseType > 0)
			{
	#ifdef TESTDEF
				fprintf(stderr,"deal698RequestResponse Buf[%d] = \n",dataLen);
				INT16U prtIndex =0;
				for(prtIndex = 0;prtIndex < dataLen;prtIndex++)
				{
					fprintf(stderr,"%02x ",recvbuff[apduDataStartIndex+prtIndex]);
					if((prtIndex+1)%20 ==0)
					{
						fprintf(stderr,"\n");
					}
				}
	#endif
				OADDATA_SAVE oadListContent[ROAD_OADS_NUM];
				memset(oadListContent,0,ROAD_OADS_NUM*sizeof(OADDATA_SAVE));

				INT8U rcvCSDnum = 0,recordNum = 0;
				INT16U oaddataLen = 0;

				oaddataLen = parseSingleROADDataHead(&recvbuff[apduDataStartIndex],oadListContent,&rcvCSDnum,&recordNum);
				INT8U csdIndex = 0,index2021 = 0;
				for(csdIndex = 0;csdIndex < rcvCSDnum;csdIndex++)
				{
					if(oadListContent[csdIndex].oad_r.OI == DATA_TIMESTAMP_OI)
					{
						index2021 = csdIndex;
						break;
					}
				}
				apduDataStartIndex += oaddataLen;
				DbgPrintToFile1(port485,"\n　曲线记录个数 = %d",recordNum);
				if(rcvCSDnum > 0 && recordNum > 0)
				{
					INT8U recordIndex = 0;
					for(recordIndex = 0;recordIndex < recordNum;recordIndex++)
					{
						oaddataLen = parseSingleROADDataBody(&recvbuff[apduDataStartIndex],oadListContent,rcvCSDnum);
						apduDataStartIndex += oaddataLen;
						dataCount += rcvCSDnum;

						TS freezeTimeStamp;
						if(oadListContent[index2021].data[0] == 0x1c)
						{
							freezeTimeStamp.Year = oadListContent[index2021].data[1];
							freezeTimeStamp.Year = freezeTimeStamp.Year<<8;
							freezeTimeStamp.Year += oadListContent[index2021].data[2];
							freezeTimeStamp.Month = oadListContent[index2021].data[3];
							freezeTimeStamp.Day = oadListContent[index2021].data[4];
							freezeTimeStamp.Hour = oadListContent[index2021].data[5];
							freezeTimeStamp.Minute = oadListContent[index2021].data[6];
							freezeTimeStamp.Sec = oadListContent[index2021].data[7];
							freezeTimeStamp.Week = 0;
							if(getZone("ZheJiang") == 0)
							{
									oadListContent[rcvCSDnum].oad_m.OI = 0x6040;
									oadListContent[rcvCSDnum].oad_m.attflg = 0x02;
									oadListContent[rcvCSDnum].oad_m.attrindex = 0;
									oadListContent[rcvCSDnum].datalen = fill_date_time_s(oadListContent[rcvCSDnum].data, &st6035->starttime);

									DateTimeBCD nowTime;
									DataTimeGet(&nowTime);
									oadListContent[rcvCSDnum+1].oad_m.OI = 0x6041;
									oadListContent[rcvCSDnum+1].oad_m.attflg = 0x02;
									oadListContent[rcvCSDnum+1].oad_m.attrindex = 0;
									oadListContent[rcvCSDnum+1].datalen = fill_date_time_s(oadListContent[rcvCSDnum+1].data, &nowTime);
									saveREADOADdata(st6035->taskID,to6001.basicinfo.addr,oadListContent,rcvCSDnum+2,freezeTimeStamp);
							}
							else
							{
								saveREADOADdata(st6035->taskID,to6001.basicinfo.addr,oadListContent,rcvCSDnum,freezeTimeStamp);
							}
						}

					}
				}

			}
		}
	}
	if(dataCount > 0)
	{
		increase6035SuccNum(st6035->taskID,to6001.sernum);
	}
	return dataCount;
}
INT16S deal6015_698(CLASS_6015 st6015, CLASS_6001 to6001,CLASS_6035* st6035,INT8U port485)
{

	fprintf(stderr, "\n deal6015_698-------------------  meter = %d\n", to6001.sernum);
	DbgPrintToFile1(port485,"普通采集方案 698测量点   meter = %d 任务号 = %d 采集数据项个数 = %d---------",
			to6001.sernum, st6015.sernum, st6015.csds.num);
	INT8U getResponseType = 0,dataCount = 0,dataCountIndex = 0;

	INT16S sendLen = 0;
	INT16S recvLen = 0;
	INT8U subindex = 0;
	INT8U sendbuff[BUFFSIZE512];
	INT8U recvbuff[BUFFSIZE2048];

	memset(sendbuff, 0, BUFFSIZE512);

	sendLen = composeProtocol698_GetRequest_RN(sendbuff, st6015,to6001.basicinfo.addr);
	if(sendLen < 0)
	{
		fprintf(stderr,"deal6015_698  sendLen < 0");
		return dataCount;
	}

	subindex = 0;
	while(subindex < MAX_RETRY_NUM_698)
	{
		memset(recvbuff, 0, BUFFSIZE2048);
		SendDataTo485(port485, sendbuff, sendLen);
		st6035->sendMsgNum++;

		recvLen = ReceDataFrom485(DLT_698,port485, 500, recvbuff);

		fprintf(stderr,"\n\n recvLen = %d \n",recvLen);
		if(recvLen > 0)
		{
			st6035->rcvMsgNum++;
			INT8U csdNum = 0;
			INT16S dataLen = recvLen;
			INT8U apduDataStartIndex = 0;
			getResponseType = analyzeProtocol698_RN(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
			fprintf(stderr,"\n getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);
			if(getResponseType > 0)
			{
	#ifdef TESTDEF
				fprintf(stderr,"deal698RequestResponse Buf[%d] = \n",dataLen);
				INT16U prtIndex =0;
				for(prtIndex = 0;prtIndex < dataLen;prtIndex++)
				{
					fprintf(stderr,"%02x ",recvbuff[apduDataStartIndex+prtIndex]);
					if((prtIndex+1)%20 ==0)
					{
						fprintf(stderr,"\n");
					}
				}
	#endif
				OADDATA_SAVE oadListContent[OADS_NUM_485_ONCEREAD];
				memset(oadListContent,0,OADS_NUM_485_ONCEREAD*sizeof(OADDATA_SAVE));
				INT16U apdudatalen = 0;
				dataCount = deal698RequestResponse(getResponseType,csdNum,&recvbuff[apduDataStartIndex],oadListContent,&apdudatalen);

				//日冻结要判断冻结时标
				if((st6015.cjtype==TYPE_FREEZE)&&(st6015.csds.csd[0].csd.road.oad.OI == 0x5004))
				{
					INT8U isTimeSame = checkTimeStamp698(oadListContent);
					if(isTimeSame == 0)
					{
						return 0;
					}
				}
				if(dataCount > 0)
				{
					increase6035SuccNum(st6035->taskID,to6001.sernum);
				}
				//存储数据
				TS OADts;
				TSGet(&OADts);

				if(getZone("ZheJiang") == 0)
				{
						oadListContent[dataCount].oad_m.OI = 0x6040;
						oadListContent[dataCount].oad_m.attflg = 0x02;
						oadListContent[dataCount].oad_m.attrindex = 0;
						oadListContent[dataCount].datalen = fill_date_time_s(oadListContent[dataCount].data, &st6035->starttime);

						DateTimeBCD nowTime;
						TsToTimeBCD(OADts, &nowTime);
						oadListContent[dataCount+1].oad_m.OI = 0x6041;
						oadListContent[dataCount+1].oad_m.attflg = 0x02;
						oadListContent[dataCount+1].oad_m.attrindex = 0;
						oadListContent[dataCount+1].datalen = fill_date_time_s(oadListContent[dataCount+1].data, &nowTime);
						saveREADOADdata(st6035->taskID,to6001.basicinfo.addr,oadListContent,dataCount+2,OADts);
				}
				else
				{
					saveREADOADdata(st6035->taskID,to6001.basicinfo.addr,oadListContent,dataCount,OADts);
				}
				//判断事件
				if((getResponseType < GET_REQUEST_RECORD)&&(st6035->taskID > 0))
				{
					for(dataCountIndex = 0;dataCountIndex < dataCount;dataCountIndex++)
					{
						checkEvent698(oadListContent[dataCountIndex].oad_r,oadListContent[dataCountIndex].data,oadListContent[dataCountIndex].datalen,to6001,st6035->taskID);
					}
				}

			}

		}
		subindex++;
	}
	if(dataCount == 0)
	{
		fprintf(stderr,"\n 抄表失败　　Event_310F \n");
		Event_310F(to6001.basicinfo.addr,NULL,0,JProgramInfo);
	}

	return dataCount;
}
/*
 * DI07List[10][4]是一个CSD对应的07数据标识列表
 * dataContent里保存一个任务抄上来的所有数据，不带数据标识
 * */
INT16S request698_07DataList(C601F_07Flag obj601F_07Flag, CLASS_6001 meter,INT8U* dataContent,CLASS_6035* st6035,INT8U port485)
{
	INT16S DataLen = 0;	//暂存正常抄读的数据长度
	INT8U index;
	INT16S singleBuffLen = 0;
	INT8U isSuccess = 1;
	fprintf(stderr,"\n\n-------request698_07DataList obj601F_07Flag.dinum = %d",obj601F_07Flag.dinum);
	for (index = 0; index < obj601F_07Flag.dinum; index++)
	{
		singleBuffLen = request698_07Data(obj601F_07Flag.DI_1[index],&dataContent[DataLen],meter,st6035,port485);

		if(singleBuffLen > 0)
		{
			DataLen += singleBuffLen;
		}
		else
		{
			isSuccess = 0;
		}

	}
	if(isSuccess !=1)
	{
		//485抄表失败
		//Event_310F()
	}
	return DataLen;
}
/*
 * DI07List[10][4]是一个CSD对应的07数据标识列表
 * dataContent里保存一个任务抄上来的所有数据，不带数据标识
 * */
INT16S request698_97DataList(C601F_97Flag obj601F_97Flag, CLASS_6001 meter,INT8U* dataContent,CLASS_6035* st6035,INT8U port485)
{
	INT16S DataLen = 0;	//暂存正常抄读的数据长度
	INT8U index;
	INT16S singleBuffLen = 0;
	INT8U isSuccess = 1;

	for (index = 0; index < obj601F_97Flag.dinum; index++)
	{
		singleBuffLen = request698_97Data(obj601F_97Flag.DI_1[index],&dataContent[DataLen],meter,st6035,port485);

		if(singleBuffLen > 0)
		{
			DataLen += singleBuffLen;
		}
		else
		{
			isSuccess = 0;
		}

	}
	if(isSuccess !=1)
	{
		//Event_310F()
	}
	return DataLen;
}
INT16S request9707_singleOAD(INT8U protocol,OI_698 roadOI,OAD soureOAD,CLASS_6001 to6001,CLASS_6035* st6035,INT8U* dataContent,INT8U port485)
{
	INT16S datalen = 0;
#ifndef	SAVETESTDEF
	//存储要求的固定长度 长度不够后面补0
	INT16U formatLen = CalcOIDataLen(soureOAD.OI,soureOAD.attrindex);
	fprintf(stderr,"\n formatLen = %d",formatLen);
	memset(dataContent,0,formatLen);
#endif
	C601F_645 Flag645;
	memset(&Flag645,0,sizeof(C601F_645));

	if(protocol == DLT_645_97)
	{
		Flag645.protocol = DLT_645_97;
		if (OADMap07DI(roadOI,soureOAD, &Flag645) == 1)
		{
			datalen = request698_97DataList(Flag645.DI._97, to6001,dataContent,st6035,port485);

			fprintf(stderr,"\n deal6015_97 datalen=%d\n",datalen);
			//特殊处理需量类数据
			if((datalen > 0)&&((soureOAD.OI == 0x1010)||(soureOAD.OI == 0x1020)))
			{
				//01 05 06 XX XX XX XX 06 XX XX XX XX 06 XX XX XX XX 06 XX XX XX XX 06 XX XX XX XX
				//01 05 1c XX XX XX XX XX XX XX 1c XX XX XX XX XX XX XX 1c XX XX XX XX XX XX XX 1c XX XX XX XX XX XX XX 1c XX XX XX XX XX XX XX
#if 0
				INT8U prtIndex = 0;
				for(prtIndex = 0;prtIndex < 75;prtIndex++)
				{
					fprintf(stderr,"%02x ",dataContent[prtIndex]);
				}
#endif
				if(soureOAD.attrindex==0)
				{
					INT8U resultbuf[75] = {0};

					INT8U rateIndex = 0;
					for(rateIndex = 0;rateIndex < dataContent[1];rateIndex++)
					{
						resultbuf[rateIndex*15] = dtstructure;
						resultbuf[rateIndex*15+1] = 2;
						memcpy(&resultbuf[rateIndex*15+2],&dataContent[2+rateIndex*5],5);
						memcpy(&resultbuf[rateIndex*15+7],&dataContent[29+rateIndex*8],8);
					}
					memcpy(&dataContent[2],resultbuf,75);
					datalen = 77;
				}
				else
				{
					INT8U resultbuf[13] = {0};
					memcpy(resultbuf,dataContent,13);
					dataContent[0] = dtstructure;
					dataContent[1] = 2;
					memcpy(&dataContent[2],resultbuf,13);
					datalen = 15;
				}
			}
		}
		else
		{
			fprintf(stderr, "request698_07Data:1");
			DbgPrintToFile1(port485,"roadOI = %04x soureOAD=%04x%02x%02x 没有对应的07数据项",roadOI,soureOAD.OI,soureOAD.attflg,soureOAD.attrindex);
		}
	}

	if(protocol == DLT_645_07)
	{
		Flag645.protocol = DLT_645_07;

		if (OADMap07DI(roadOI,soureOAD, &Flag645) == 1)
		{
			datalen = request698_07DataList(Flag645.DI._07, to6001,dataContent,st6035,port485);

			fprintf(stderr,"\n deal6015_07 datalen=%d",datalen);

		}
		else
		{
			fprintf(stderr, "request698_07Data:1");
			DbgPrintToFile1(port485,"roadOI = %04x soureOAD=%04x%02x%02x 没有对应的07数据项",roadOI,soureOAD.OI,soureOAD.attflg,soureOAD.attrindex);
		}
	}


	return datalen;

}

//抄日冻结数据之前，先抄时标看是否匹配
//index=1，上一次
INT8S checkTimeStamp07(CLASS_6001 obj6001,INT8U port485)
{
	INT8S result = 0;
	INT16S retlen = 0;
	CLASS_6035 invalidst6035;
	INT8U dataContent[BUFFSIZE128];
	memset(dataContent,0,BUFFSIZE128);

	retlen = request698_07Data(freezeflag07_1,dataContent,obj6001,&invalidst6035,port485);
	result = isTimerSame(0,dataContent);
	if(retlen <= 0)
	{
		memset(dataContent,0,BUFFSIZE128);
		request698_07Data(freezeflag07_1,dataContent,obj6001,&invalidst6035,port485);
		result = isTimerSame(0,dataContent);
	}

//	DbPrt1(port485,"checkTimeStamp07 buff:", (char *) dataContent, 10, NULL);
	return result;
}

INT8U realtoFreeze(CSD freezecsd, CSD *realcsd,INT8U port485)
{
	TS ts_now;
	TSGet(&ts_now);
	if(ts_now.Hour >= 2)
	{
		DbgPrintToFile1(port485, "切换实时数据读取");
		memcpy(realcsd,&freezecsd,sizeof(CSD));
		if(realcsd->road.oad.OI == 0x5004)
		{
			realcsd->road.oad.OI = 0x0000;
		}
		return 1;
	}
	return 0;
}

INT16S deal6015_9707(INT8U protocol,CLASS_6015 st6015, CLASS_6001 to6001,CLASS_6035* st6035,INT8U port485) {
	INT16S datalen = 0,totaldataLen = 0;
	INT8U oadDataNum = 0,isAllSucc = 0;

	CSD 	realcsd={};
	INT8U 	realflg = 0;

	fprintf(stderr,
			"\n\n-------start------------ deal6015_07  meter = %d st6015.sernum = %d st6015.csds.num = %d---------",
			to6001.sernum, st6015.sernum, st6015.csds.num);
	DbgPrintToFile1(port485,"普通采集方案 07测量点   meter = %d 任务号 = %d 采集数据项个数 = %d---------",
			to6001.sernum, st6015.sernum, st6015.csds.num);
#ifdef SAVETESTDEF
	INT8U dataContent[BUFFSIZE256];
	OADDATA_SAVE OADdata[ROAD_OADS_NUM];
	memset(OADdata,0,ROAD_OADS_NUM*sizeof(OADDATA_SAVE));

#endif
	INT8U dataIndex = 0;
	for (dataIndex = 0; dataIndex < st6015.csds.num; dataIndex++)
	{
		memset(dataContent,0,BUFFSIZE256);
		//ROAD
		if(st6015.csds.csd[dataIndex].type == 1)
		{
			INT8S isFreezeValid = 1;
			//日冻结检查时标
			if((st6015.csds.csd[dataIndex].csd.road.oad.OI == 0x5004)&&(st6015.cjtype==TYPE_FREEZE)&&(protocol==DLT_645_07))
			{
				isFreezeValid = checkTimeStamp07(to6001,port485);
			}
			if(isFreezeValid==0)
			{
				DbgPrintToFile1(port485, "冻结时标不正确");
				if(JProgramInfo->cfg_para.extpara[1] == 1) { //实时转冻结方式
					realflg = realtoFreeze(st6015.csds.csd[dataIndex].csd,&realcsd,port485);
					if(realflg == 0) continue;
				}else {
					realflg = 0;
					continue;
				}
			}
			INT8U isCurveROAD = 0;
			//对于07表　曲线就是抄实时数据
			if(st6015.csds.csd[dataIndex].csd.road.oad.OI == 0x5002)
			{
				st6015.csds.csd[dataIndex].csd.road.oad.OI = 0x0000;
				isCurveROAD = 1;
			}

			INT8U csdIndex;
			for(csdIndex=0;csdIndex<st6015.csds.csd[dataIndex].csd.road.num;csdIndex++)
			{
				memset(dataContent,0,BUFFSIZE256);
				INT8S ret = dealRealTimeRequst(port485);
				if(ret == PARA_CHANGE_RETVALUE)
				{
					return PARA_CHANGE_RETVALUE;
				}
				if((isCurveROAD==1)&&(st6015.csds.csd[dataIndex].csd.road.oads[csdIndex].OI == DATA_TIMESTAMP_OI))
				{
					DateTimeBCD startTime;
					DataTimeGet(&startTime);
					startTime.sec.data = 0;
					startTime.min.data = 0;
					datalen = 8;
					fill_date_time_s(dataContent,&startTime);
				}
				else
				{
					if(realflg == 1){
						datalen = request9707_singleOAD(protocol,realcsd.road.oad.OI,
								realcsd.road.oads[csdIndex],to6001,st6035,dataContent,port485);
					}else {
						datalen = request9707_singleOAD(protocol,st6015.csds.csd[dataIndex].csd.road.oad.OI,
							st6015.csds.csd[dataIndex].csd.road.oads[csdIndex],to6001,st6035,dataContent,port485);
					}
				}

				if(datalen > 0)
				{
					totaldataLen += datalen;

					OADdata[oadDataNum].oad_m = st6015.csds.csd[dataIndex].csd.road.oad;
					if(isCurveROAD == 1)
					{
						OADdata[oadDataNum].oad_m.OI = 0x5002;
					}
					OADdata[oadDataNum].oad_r = st6015.csds.csd[dataIndex].csd.road.oads[csdIndex];
					OADdata[oadDataNum].datalen = datalen;
					memcpy(OADdata[oadDataNum].data,dataContent,datalen);
					fprintf(stderr,"\n********* OADdata[%d] oad_m = %04x oad_r = %04x data[%d] = ",oadDataNum,OADdata[oadDataNum].oad_m.OI,OADdata[oadDataNum].oad_r.OI,datalen);
					INT8U prtIndex = 0;
					for(prtIndex = 0;prtIndex < datalen;prtIndex++)
					{
						fprintf(stderr,"%02x ",OADdata[oadDataNum].data[prtIndex]);
					}
					oadDataNum++;
				}

			}
			if(oadDataNum == st6015.csds.csd[dataIndex].csd.road.num)
			{
				isAllSucc = 1;
			}

		}
		else
		{
			INT8S ret = dealRealTimeRequst(port485);
			if(ret == PARA_CHANGE_RETVALUE)
			{
				return PARA_CHANGE_RETVALUE;
			}
			if(st6015.csds.csd[dataIndex].csd.oad.OI == DATA_TIMESTAMP_OI)
			{
				DateTimeBCD startTime;
				DataTimeGet(&startTime);
				datalen = 8;
				fill_date_time_s(dataContent,&startTime);
			}
			else
			{
				datalen = request9707_singleOAD(protocol,0x0000,st6015.csds.csd[dataIndex].csd.oad,to6001,st6035,dataContent,port485);
			}
			totaldataLen += datalen;
			memset(&OADdata[oadDataNum].oad_m,0,sizeof(OAD));
			OADdata[oadDataNum].oad_r = st6015.csds.csd[dataIndex].csd.oad;
			OADdata[oadDataNum].datalen = datalen;
			memcpy(OADdata[oadDataNum].data,dataContent,datalen);
			fprintf(stderr,"\n********** OADdata[%d] oad_m = %04x oad_r = %04x data[%d] = ",dataIndex,OADdata[oadDataNum].oad_m.OI,OADdata[oadDataNum].oad_r.OI,datalen);
			INT8U prtIndex = 0;
			for(prtIndex = 0;prtIndex < datalen;prtIndex++)
			{
				fprintf(stderr,"%02x ",OADdata[oadDataNum].data[prtIndex]);
			}
			oadDataNum++;
		}
		if(oadDataNum>=ROAD_OADS_NUM)
		{
			break;
		}
		if(oadDataNum == st6015.csds.num)
		{
			isAllSucc = 1;
		}
	}
	if(totaldataLen >= DATA_CONTENT_LEN)
	{
		fprintf(stderr,"dataContent 长度不够");
		fprintf(stderr,"deal6015_07 datalen = %d totaldataLen = %d",datalen,totaldataLen);
		return totaldataLen;
	}
	if(isAllSucc == 1)
	{
		increase6035SuccNum(st6035->taskID,to6001.sernum);
	}
#ifdef SAVETESTDEF
	if(totaldataLen > 0)
	{
		TS OADts;
		TSGet(&OADts);
		if(OADdata[0].oad_m.OI == 0x5002)
		{
			if(st6015.cjtype == TYPE_INTERVAL)
			{
				OADts.Sec = 0;

				INT16U minInterVal = 0;//冻结时标间隔-分钟
				if(st6015.data.data[0] == minute_units)
				{
					minInterVal = (st6015.data.data[1]<<8)+st6015.data.data[2];
					OADts.Minute = OADts.Minute/minInterVal*minInterVal;
				}
				if(st6015.data.data[0] == hour_units)
				{
					INT8U hourInterVal = (st6015.data.data[1]<<8)+st6015.data.data[2];
					minInterVal = hourInterVal*60;
					OADts.Minute = 0;
					OADts.Hour = OADts.Hour/hourInterVal*hourInterVal;
				}

			}
		}

		if(getZone("ZheJiang") == 0)
		{
			OADdata[oadDataNum].oad_m.OI = 0x6040;
			OADdata[oadDataNum].oad_m.attflg = 0x02;
			OADdata[oadDataNum].oad_m.attrindex = 0;
			OADdata[oadDataNum].datalen = fill_date_time_s(OADdata[oadDataNum].data, &st6035->starttime);

			DateTimeBCD nowTime;
			TsToTimeBCD(OADts, &nowTime);
			OADdata[oadDataNum+1].oad_m.OI = 0x6041;
			OADdata[oadDataNum+1].oad_m.attflg = 0x02;
			OADdata[oadDataNum+1].oad_m.attrindex = 0;
			OADdata[oadDataNum+1].datalen = fill_date_time_s(OADdata[oadDataNum+1].data, &nowTime);
			saveREADOADdata(st6035->taskID,to6001.basicinfo.addr,OADdata,oadDataNum+2,OADts);
		}
		else
		{
			saveREADOADdata(st6035->taskID,to6001.basicinfo.addr,OADdata,oadDataNum,OADts);
		}
	}

#endif

	fprintf(stderr,
			"\n\n**********end************ deal6015_07  meter = %d st6015.sernum = %d st6015.csds.num = %d---------",
			to6001.sernum, st6015.sernum, st6015.csds.num);

	return totaldataLen;
}
//按照任务6012格式上报
INT8U createSendEventBuffHead(ROAD roadBody,INT8U* reportEventBuf,INT8U saveContentHead[SAVE_EVENT_BUFF_HEAD_LEN])
{
	INT8U eventBufLen = 0;
	//6012
	reportEventBuf[eventBufLen++] = 0x60;
	reportEventBuf[eventBufLen++] = 0x12;
	reportEventBuf[eventBufLen++] = 0x03;
	reportEventBuf[eventBufLen++] = 0x00;
	//num
	reportEventBuf[eventBufLen++] = 0x02;
	//TSA
	reportEventBuf[eventBufLen++] = 0;
	reportEventBuf[eventBufLen++] = 0x20;
	reportEventBuf[eventBufLen++] = 0x2A;
	reportEventBuf[eventBufLen++] = 0x02;
	reportEventBuf[eventBufLen++] = 0x00;
	reportEventBuf[eventBufLen++] = 1;
	//ROAD event
//	eventBufLen += OADtoBuff(roadBody.oad,&reportEventBuf[eventBufLen]);
	eventBufLen += create_OAD(0,&reportEventBuf[eventBufLen],roadBody.oad);
	reportEventBuf[eventBufLen++] = roadBody.num;

	INT8U oadIndex = 0;
	for(oadIndex = 0;oadIndex < roadBody.num;oadIndex++)
	{
//		eventBufLen += OADtoBuff(roadBody.oads[oadIndex],&reportEventBuf[eventBufLen]);
		eventBufLen += create_OAD(0,&reportEventBuf[eventBufLen],roadBody.oads[oadIndex]);
	}

	reportEventBuf[eventBufLen++] = 1;
	reportEventBuf[eventBufLen++] = 1;

	INT8U addrLen = saveContentHead[1]+2;
	memcpy(&reportEventBuf[eventBufLen],&saveContentHead[0],addrLen);
	eventBufLen += addrLen;

	reportEventBuf[eventBufLen++] = 1;
	reportEventBuf[eventBufLen++] = roadBody.num;

	memcpy(&reportEventBuf[eventBufLen],&saveContentHead[18],21);
	eventBufLen += 21;
	return eventBufLen;
}

INT8S sendEventReportBuff698(ROAD eventRoad,INT8U saveContentHead[SAVE_EVENT_BUFF_HEAD_LEN],INT8U port485,OADDATA_SAVE* oadListContent)
{
	INT8S ret = -1;
	INT8U eventBufLen = 0;
	INT8U reportEventBuf[BUFFSIZE256];
	memset(reportEventBuf,0,BUFFSIZE256);

	eventBufLen = createSendEventBuffHead(eventRoad,reportEventBuf,saveContentHead);


	INT8U oadIndex = 0;
	for(oadIndex = 3;oadIndex < eventRoad.num;oadIndex++)
	{
		INT8U isFind = 0;
		INT8U subIndex = 0;
		for(subIndex =0;subIndex<ROAD_OADS_NUM;subIndex++)
		{
			if(memcmp(&eventRoad.oads[oadIndex],&oadListContent[subIndex].oad_r,sizeof(OAD))==0)
			{
				memcpy(&reportEventBuf[eventBufLen],oadListContent[subIndex].data,oadListContent[subIndex].datalen);
				eventBufLen += oadListContent[subIndex].datalen;
				isFind = 1;
				break;
			}
		}
		if(isFind == 0)
		{
			reportEventBuf[eventBufLen++] = 0;
		}
	}
	DbPrt1(port485,"698 上报事件 buff:", (char *) reportEventBuf, eventBufLen, NULL);
	//TODO 发送消息
	OAD	oad={};
	mqs_send((INT8S *)PROXY_NET_MQ_NAME,1,METEREVENT_REPORT,oad,reportEventBuf,eventBufLen);
	return ret;
}
INT16S deal6017_698(CLASS_6015 st6015, CLASS_6001 to6001,CLASS_6035* st6035,INT8U port485)
{
	INT16U totaldataLen =0;
#if 0
	fprintf(stderr,"事件采集方案 698测量点   meter = %d 任务号 = %d 采集数据项个数 = %d---------",
			to6001.sernum, st6015.sernum, st6015.csds.num);

	DbgPrintToFile1(port485,"事件采集方案 698测量点   meter = %d 任务号 = %d 采集数据项个数 = %d---------",
			to6001.sernum, st6015.sernum, st6015.csds.num);
#endif

	INT8U getResponseType = 0;
	INT16S retLen = 0;
	INT16S sendLen = 0;
	INT16S recvLen = 0;
	INT8U subindex = 0;
	INT8U sendbuff[BUFFSIZE256];
	INT8U recvbuff[BUFFSIZE2048];


#ifdef TESTDEF
	CLASS_6015 singleRoadEvent;
	memset(&singleRoadEvent,0,sizeof(CLASS_6015));
	singleRoadEvent.csds.num = 1;
	singleRoadEvent.cjtype = TYPE_LAST;
	singleRoadEvent.csds.csd[0].type = 1;

	INT8U csdIndex = 0;
	for(csdIndex = 0;csdIndex < st6015.csds.num;csdIndex++)
	{
		memset(sendbuff, 0, BUFFSIZE256);
		INT8S ret = dealRealTimeRequst(port485);
		if(ret == PARA_CHANGE_RETVALUE)
		{
			return PARA_CHANGE_RETVALUE;
		}
		memcpy(&singleRoadEvent.csds.csd[0].csd.road,&st6015.csds.csd[csdIndex].csd.road,sizeof(ROAD));

		sendLen = composeProtocol698_GetRequest_RN(sendbuff, singleRoadEvent, to6001.basicinfo.addr);

		if(sendLen < 0)
		{
			fprintf(stderr,"deal6015_698  sendLen < 0");
			return retLen;
		}
	//	fprintf(stderr,"\n\n1------------comfd1 = %d",comfd485[0]);
		subindex = 0;
		while(subindex < MAX_RETRY_NUM_698)
		{
			memset(recvbuff, 0, BUFFSIZE2048);
			SendDataTo485(port485, sendbuff, sendLen);
			st6035->sendMsgNum++;
			recvLen = ReceDataFrom485(DLT_698,port485, 1000, recvbuff);
			fprintf(stderr,"\n\n recvLen = %d \n",recvLen);
			if(recvLen > 0)
			{
				st6035->rcvMsgNum++;
				INT8U csdNum = 0;
				INT16S dataLen = recvLen;
				INT8U apduDataStartIndex = 0;
				getResponseType = analyzeProtocol698_RN(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
				//fprintf(stderr,"\n getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);

				if(getResponseType > 0)
				{
					OADDATA_SAVE oadListContent[ROAD_OADS_NUM];

					INT8U rcvCSDnum = 0,recordNum = 0;
					apduDataStartIndex += parseSingleROADDataHead(&recvbuff[apduDataStartIndex],oadListContent,&rcvCSDnum,&recordNum);
					parseSingleROADDataBody(&recvbuff[apduDataStartIndex],oadListContent,rcvCSDnum);

					if(rcvCSDnum > 0)
					{
						TS OADts;
						TSGet(&OADts);
						INT16U isEventOccur = saveREADOADevent(singleRoadEvent.csds.csd[0].csd.road,to6001.basicinfo.addr,oadListContent,rcvCSDnum,OADts);
						DbgPrintToFile1(port485,"isEventOccur　＝%d st6015.savetimeflag = %d 事件序号= %02x %02x %02x %02x %02x", isEventOccur,st6015.savetimeflag,
								oadListContent[0].data[0],oadListContent[0].data[1],oadListContent[0].data[2],oadListContent[0].data[3],oadListContent[0].data[4]);
						if((isEventOccur == 1)&&(isAllowReport==1)&&(st6015.savetimeflag==1))//st6015->savetimeflag = st6017.ifreport;
						{
							INT8U saveContentHead[SAVE_EVENT_BUFF_HEAD_LEN];//TAS + 事件序号　+ 发生时间　＋　结束时间
							memset(saveContentHead,0,SAVE_EVENT_BUFF_HEAD_LEN);

							saveContentHead[0] = dttsa;
							memcpy(&saveContentHead[1],to6001.basicinfo.addr.addr,sizeof(TSA));//采集通信地址//TAS
							memcpy(&saveContentHead[18],oadListContent[0].data,oadListContent[0].datalen);//事件序号
							memcpy(&saveContentHead[23],oadListContent[1].data,oadListContent[1].datalen);//发生时间
							memcpy(&saveContentHead[31],oadListContent[2].data,oadListContent[2].datalen);//结束时间


							sendEventReportBuff698(singleRoadEvent.csds.csd[0].csd.road,saveContentHead,port485,oadListContent);

						}


					}
					break;
				}
			}
			subindex++;
		}
	}
#else//下面的是按照getrecordList 做的有待测试
	sendLen = composeProtocol698_GetRequest_RN(sendbuff, st6015, to6001.basicinfo.addr);
	if(sendLen < 0)
	{
		fprintf(stderr,"deal6015_698  sendLen < 0");
		return retLen;
	}

	subindex = 0;
	while(subindex < 3)
	{
		memset(recvbuff, 0, BUFFSIZE512);
		SendDataTo485(port485, sendbuff, sendLen);
		st6035->sendMsgNum++;
		recvLen = ReceDataFrom485(DLT_698,port485, 500, recvbuff);
		fprintf(stderr,"\n\n recvLen = %d \n",recvLen);
		if(recvLen > 0)
		{
			st6035->rcvMsgNum++;
			INT8U csdNum = 0;
			INT16S dataLen = recvLen;
			INT8U apduDataStartIndex = 0;
			getResponseType = analyzeProtocol698_RN(recvbuff,&csdNum,recvLen,&apduDataStartIndex,&dataLen);
			fprintf(stderr,"\n getResponseType = %d  csdNum = %d dataLen = %d \n",getResponseType,csdNum,dataLen);
			if(getResponseType > 0)
			{
				//retLen = deal698RequestResponse(0,getResponseType,csdNum,&recvbuff[apduDataStartIndex],dataContent,st6015.csds,to6001,st6035->taskID,st6015.cjtype);
				break;
			}

		}
		subindex++;
	}
#endif
	fprintf(stderr, "\n deal6015_698-------------------  retLen = %d\n", retLen);
	return totaldataLen;
}
INT8S sendEventReportBuff07(INT8U port485,ROAD roadBody,INT8U saveContentHead[SAVE_EVENT_BUFF_HEAD_LEN],INT8U* dataContent,INT16U datalen)
{
	INT8S ret = -1;
	INT8U eventBufLen = 0;
	INT8U reportEventBuf[BUFFSIZE256];
	memset(reportEventBuf,0,BUFFSIZE256);

	eventBufLen = createSendEventBuffHead(roadBody,reportEventBuf,saveContentHead);

	//开表盖
	if(roadBody.oad.OI == 0x301b)
	{
		memcpy(&reportEventBuf[eventBufLen],&dataContent[16],datalen-16);
		eventBufLen = eventBufLen + datalen-16;
	}
	DbPrt1(port485,"上报事件 buff:", (char *) reportEventBuf, eventBufLen, NULL);
	//TODO 发送消息
	OAD	oad={};
	mqs_send((INT8S *)PROXY_NET_MQ_NAME,1,METEREVENT_REPORT,oad,reportEventBuf,eventBufLen);
	return ret;
}
INT16S deal6017_07(CLASS_6015 st6015, CLASS_6001 to6001,CLASS_6035* st6035,INT8U port485)
{
	fprintf(stderr,"\n\n 抄电表事件  meter = %d st6015.sernum = %d---------",to6001.sernum, st6015.sernum);
	DbgPrintToFile1(port485,"抄电表事件 deal6017_07  meter = %d st6015.sernum = %d---------",to6001.sernum, st6015.sernum);
	INT8U dataContent[BUFFSIZE256];
	OI_698 eventOI = 0x0001;
	INT8U dataIndex = 0;

	for (dataIndex = 0; dataIndex < st6015.csds.num; dataIndex++)
	{
		INT8S ret = dealRealTimeRequst(port485);

		if(ret == PARA_CHANGE_RETVALUE)
		{
			return PARA_CHANGE_RETVALUE;
		}
		//ROAD
		if(st6015.csds.csd[dataIndex].type != 1)
		{
			continue;
		}

		INT8U saveContentHead[SAVE_EVENT_BUFF_HEAD_LEN];//TSA+事件序号+发生时间+结束时间
		memset(saveContentHead,0,SAVE_EVENT_BUFF_HEAD_LEN);
		memset(dataContent,0,DATA_CONTENT_LEN);

		INT16S headDataLen =0;

		saveContentHead[headDataLen++] = dttsa;
		memcpy(&saveContentHead[headDataLen],to6001.basicinfo.addr.addr,sizeof(TSA));//采集通信地址
		headDataLen += sizeof(TSA);

		//请求07表的事件次数
		OAD eventRoad;
		eventRoad.OI = st6015.csds.csd[dataIndex].csd.road.oad.OI;
		eventRoad.attflg = st6015.csds.csd[dataIndex].csd.road.oad.attflg;
		eventRoad.attrindex = st6015.csds.csd[dataIndex].csd.road.oad.attrindex;
		DbgPrintToFile1(port485,"请求07表的事件 = %04x%02x%02x",eventRoad.OI,eventRoad.attflg,eventRoad.attrindex);

		C601F_645 Flag645;
		memset(&Flag645,0,sizeof(C601F_645));
		Flag645.protocol = DLT_645_07;

		if(OADMap07DI(eventOI,eventRoad, &Flag645) == 1)
		{
			request698_07Data(Flag645.DI._07.DI_1[0],dataContent,to6001,st6035,port485);
		}
		else
		{
			DbgPrintToFile1(port485,"07DI_698OAD.cfg中没有对应的数据项");
			continue;
		}
		DbgPrintToFile1(port485,"deal6017_07　事件序号　%02x%02x%02x%02x%02x",dataContent[0],dataContent[1],dataContent[2],dataContent[3],dataContent[4]);
		DbgPrintToFile1(port485,"zeroBuff　%02x%02x%02x%02x",zeroBuff[0],zeroBuff[1],zeroBuff[2],zeroBuff[3]);
#if 0
		if(memcmp(&dataContent[1],zeroBuff,4)==0)
		{
			continue;
		}
#else
		if(dataContent[1]==0 && dataContent[2]==0  && dataContent[3]==0 && dataContent[4]==0) {
			continue;
		}
#endif
		//事件序号
		memcpy(&saveContentHead[headDataLen],dataContent,5);
		headDataLen += 5;

		//请求07表的事件记录
		Flag645.DI._07.DI_1[0][0] = 1;
		memset(dataContent,0,DATA_CONTENT_LEN);
		INT16U datalen = request698_07Data(Flag645.DI._07.DI_1[0],dataContent,to6001,st6035,port485);
		memcpy(&saveContentHead[headDataLen],dataContent,16);
		headDataLen += 16;
		TS ts_cc;
		TSGet(&ts_cc);
		DbPrt1(port485,"07 存储事件 buff:", (char *) saveContentHead, headDataLen, NULL);
//		int isEventOccur = SaveNorData(st6035->taskID,&st6015.csds.csd[dataIndex].csd.road,saveContentHead,headDataLen,ts_cc);
//		DbgPrintToFile1(port485,"isEventOccur = %d", isEventOccur);
//		if(isEventOccur == 1)
//		{
//			sendEventReportBuff07(port485,st6015.csds.csd[dataIndex].csd.road,saveContentHead,dataContent,datalen);
//		}
	}

	return 0;
}
INT8U getsub6015(INT8U subIndex,CLASS_6015 source6015,CLASS_6015* desst6015)
{
	memset(desst6015,0,sizeof(CLASS_6015));
	INT8U ret = 0;
	if (source6015.cjtype == TYPE_NULL)
	{
		memcpy(desst6015,&source6015,sizeof(CLASS_6015));
		if(subIndex==0)
		{
			desst6015->csds.num = OADS_NUM_485_ONCEREAD;
			return ret;
		}
		else
		{
			if(desst6015->csds.num >= (subIndex+1)*OADS_NUM_485_ONCEREAD)
			{
				desst6015->csds.num = OADS_NUM_485_ONCEREAD;
			}
			else
			{
				desst6015->csds.num -= subIndex*OADS_NUM_485_ONCEREAD;
			}
			memset(desst6015->csds.csd,0,sizeof(MY_CSD)*MY_CSD_NUM);
			memcpy(desst6015->csds.csd,&source6015.csds.csd[subIndex*OADS_NUM_485_ONCEREAD],sizeof(MY_CSD)*desst6015->csds.num);
		}

	}
	return ret;
}
/*
 * 抄读1个测量点
 */
INT16S deal6015or6017_singlemeter(CLASS_6013 st6013,CLASS_6015 st6015,CLASS_6001 obj6001,CLASS_6035* st6035,INT8U port485)
{
	INT16S ret = 0;

	ret = dealRealTimeRequst(port485);
	if(ret == PARA_CHANGE_RETVALUE)
	{
		return PARA_CHANGE_RETVALUE;
	}

	if(getComfdBy6001(obj6001.basicinfo.baud,port485)!=1)
	{
		return ret;
	}

	if(st6013.cjtype == norm)
	{
		fprintf(stderr,"\n 处理普通采集方案");
		switch (obj6001.basicinfo.protocol)
		{
			case DLT_645_97:
			case DLT_645_07:
				ret = deal6015_9707(obj6001.basicinfo.protocol,st6015, obj6001,st6035,port485);
			break;
			default:
				{
					//曲线　　每次抄一小时的数据
					if(st6015.cjtype != TYPE_INTERVAL)
					{
						INT8U csdcount = st6015.csds.num;

						if(st6015.cjtype !=TYPE_NULL)
						{
							csdcount = st6015.csds.csd[0].csd.road.num;
						}
						INT8U readTimes = (csdcount/OADS_NUM_485_ONCEREAD)+1;
						if(csdcount%OADS_NUM_485_ONCEREAD==0)
						{
							readTimes -= 1;
						}
						if(readTimes==1)
						{
							ret = deal6015_698(st6015,obj6001,st6035,port485);
						}
						else
						{
							//数据项超过10分开抄
							INT8U readIndex = 0;
							for (readIndex = 0;readIndex < readTimes;readIndex++)
							{
								CLASS_6015 st6015sub;
								getsub6015(readIndex,st6015,&st6015sub);
								ret = deal6015_698(st6015sub,obj6001,st6035,port485);
							}
						}


					}
					else
					{
						DbgPrintToFile1(port485,"6013任务执行频率%d-%d　6015 冻结间隔　%d-%d-%d",
														st6013.interval.units, st6013.interval.interval,
														st6015.data.data[0],st6015.data.data[1],st6015.data.data[2]);

						ret = dealCurve_698(st6015,obj6001,st6035,port485);
					}
				}
		}

	}
	if(st6013.cjtype == events)
	{
		fprintf(stderr,"\n 处理事件采集方案");
		switch (obj6001.basicinfo.protocol)
		{
			case DLT_645_07:
				//ret = deal6017_07(st6015, obj6001,st6035,port485);
			break;
			default:
				ret = deal6017_698(st6015,obj6001,st6035,port485);
		}
	}

	fprintf(stderr, "\ndeal6015_singlemeter ret = %d\n",ret);
	return ret;
}

/*
 *根据6015中的MS 和电表地址 和端口好判断此电表是否需要抄读
 *0-不抄 1-抄
 */
INT8U checkMeterType(MY_MS mst,INT8U usrType,TSA usrAddr)
{

	INT16U ms_num=0;
	INT16U collIndex = 0;

	switch(mst.mstype)
	{
		case 0:
			return 0;
			break;
		case 1:
			return 1;
			break;
		case 2:
			ms_num = (mst.ms.userType[0]<<8) | mst.ms.userType[1];
			fprintf(stderr,"2 一组用户类型：个数=%d\n 值=",ms_num);
			for(collIndex=0;collIndex < ms_num;collIndex++)
			{
				if(mst.ms.userType[collIndex+2] == usrType)
				{
					return 1;
				}
			}
			break;
		case 3:
			ms_num = (mst.ms.userAddr[0].addr[0]<<8)|mst.ms.userAddr[0].addr[1];
			fprintf(stderr,"一组用户地址：个数=%d\n ",ms_num);
			if(ms_num > COLLCLASS_MAXNUM) fprintf(stderr,"配置序号 超过限值 %d ,error !!!!!!",COLLCLASS_MAXNUM);
			for(collIndex = 0;collIndex < ms_num;collIndex++)
			{
				if(memcmp(&mst.ms.userAddr[collIndex+1],&usrAddr,sizeof(TSA)) == 0)
				{
					return 1;
				}
			}
			break;
		case 5:
			fprintf(stderr,"\n5：一组用户类型区间\n");
			for(collIndex = 0;collIndex < COLLCLASS_MAXNUM;collIndex++)
			{
				if((mst.ms.type[collIndex].type != interface)
						&&(mst.ms.type[collIndex].begin[0]|mst.ms.type[collIndex].end[0])!=0)
				{
					INT16S typeBegin = -1;
					INT16S typeEnd = -1;
					if(mst.ms.type[collIndex].begin[0] == dtunsigned)
					{
						typeBegin = mst.ms.type[collIndex].begin[1];
					}
					if(mst.ms.type[collIndex].end[0] == dtunsigned)
					{
						typeEnd = mst.ms.type[collIndex].end[1];
					}
					fprintf(stderr,"\n collIndex = %d metertype = %d typeBegin = %d typeEnd = %d \n",collIndex,usrType,typeBegin,typeEnd);
					if((usrType > typeBegin)&&(usrType < typeEnd))
					{
						return 1;
					}
					else if(mst.ms.type[collIndex].type == close_open)
					{
						if(usrType == typeBegin)
						{
							return 1;
						}
					}
					else if(mst.ms.type[collIndex].type == open_close)
					{
						if(usrType == typeEnd)
						{
							return 1;
						}
					}
					else if(mst.ms.type[collIndex].type == close_close)
					{
						if((usrType == typeBegin)||(usrType == typeEnd))
						{
							return 1;
						}
					}

				}
			}

			break;
		default :
				return 0;

	}

	return 0;
}

INT8U getSaveTime(DateTimeBCD* saveTime,INT8U cjType,INT8U saveTimeFlag,DATA_TYPE curvedata)
{
	INT8U ret = 0;
	DataTimeGet(saveTime);
	//曲线
	if(cjType== TYPE_INTERVAL)
	{
		if(curvedata.data[0] == minute_units)
		{
			INT8U minute = (curvedata.data[1]<<8)+curvedata.data[2];
			if(minute!=0)
			{
				saveTime->min.data = (saveTime->min.data/minute)*minute;
			}
		}
		if(curvedata.data[0] == hour_units)
		{
			saveTime->min.data = 0;
		}
		saveTime->sec.data = 0;
	}
	if((cjType== TYPE_LAST)||(cjType== TYPE_FREEZE))
	{
		TS nowts;
		TSGet(&nowts);
		if(saveTimeFlag == 2)
		{
			nowts.Hour = 0;
			nowts.Minute = 0;
			nowts.Sec = 0;
		}
		if(saveTimeFlag == 3)
		{
			tminc(&nowts,day_units,-1);
			nowts.Hour = 23;
			nowts.Minute = 59;
			nowts.Sec = 0;
		}
		if(saveTimeFlag == 4)
		{
			tminc(&nowts,day_units,-1);
			nowts.Hour = 0;
			nowts.Minute = 0;
			nowts.Sec = 0;
		}
		if(saveTimeFlag == 5)
		{
			nowts.Day = 1;
			nowts.Hour = 0;
			nowts.Minute = 0;
			nowts.Sec = 0;
		}
		TsToTimeBCD(nowts,saveTime);
	}

	return ret;
}
INT16U compose6012Buff(DateTimeBCD startTime,DateTimeBCD saveTime,TSA meterAddr,INT16U dataLen,INT8U* dataContent, INT8U port485)
{
	fprintf(stderr,"\n 存储数据  compose6012Buff--------------");
	INT16U index;
	INT16U bufflen = 0;
	DateTimeBCD endTime;
	DataTimeGet(&endTime);
	INT8U buff6012[DATA_CONTENT_LEN];
	memset(buff6012,0,DATA_CONTENT_LEN);

	index = bufflen;
	buff6012[bufflen++] = dttsa;
	memcpy(&buff6012[bufflen],meterAddr.addr,sizeof(TSA));//采集通信地址
	bufflen += sizeof(TSA);
	fprintf(stderr,"\n 采集通信地址：");
	for(;index < bufflen;index++)
	{
		fprintf(stderr," %02x",buff6012[index]);
	}


	index = bufflen;
	bufflen += fill_date_time_s(&buff6012[bufflen],&startTime);
	fprintf(stderr,"\n 采集启动时标：");
	fprintf(stderr,"index = %d bufflen = %d",index,bufflen);
	for(;index < bufflen;index++)
	{
		fprintf(stderr," %02x",buff6012[index]);
	}

	index = bufflen;
	bufflen += fill_date_time_s(&buff6012[bufflen],&endTime);
	fprintf(stderr,"\n 采集成功时标：");
	fprintf(stderr,"index = %d bufflen = %d",index,bufflen);
	for(;index < bufflen;index++)
	{
		fprintf(stderr," %02x",buff6012[index]);
	}

	index = bufflen;
	bufflen += fill_date_time_s(&buff6012[bufflen],&saveTime);
	fprintf(stderr,"\n 采集存储时标：");
	fprintf(stderr,"index = %d bufflen = %d",index,bufflen);
	for(;index < bufflen;index++)
	{
		fprintf(stderr," %02x",buff6012[index]);
	}

	index = bufflen;
	memcpy(&buff6012[bufflen],dataContent,dataLen);
	bufflen += dataLen;

	fprintf(stderr,"\n 采集数据内容：\n");
	for(;index < bufflen;index++)
	{
		fprintf(stderr," %02x",buff6012[index]);
		if((index+1)%20==0)
		{
			fprintf(stderr,"\n");
		}
	}
	memset(dataContent,0,DATA_CONTENT_LEN);
	memcpy(dataContent,buff6012,bufflen);
//	fprintf(stderr,"\n\n buff6012[%d]:",bufflen);
//	DbPrt1(port485,"存储数据  compose6012Buff:", (char *) dataContent, bufflen, NULL);
	return bufflen;
}
//GetOrSet 0 -get 1-set
INT8U GetOrSetFreezeDataSuccess(INT8U GetOrSet,INT8U taskID,INT8U port,INT16U mpSernum)
{
	INT8U ret = 0;
	INT8U tIndex = 0;
	//DbgPrintToFile1(1,"taskID = %d port:%d mpSernum = %d　infoReplenish.tasknum = %d",taskID,port,mpSernum,infoReplenish.tasknum);
	for(tIndex = 0;tIndex < infoReplenish.tasknum;tIndex++)
	{
		//DbgPrintToFile1(1,"infoReplenish.unitReplenish[tIndex].taskID = %d　",infoReplenish.unitReplenish[tIndex].taskID);
		if(taskID == infoReplenish.unitReplenish[tIndex].taskID)
		{
			//DbgPrintToFile1(1,"infoReplenish.unitReplenish[%d].list6001[%d].meterSum = %d　",tIndex,port,infoReplenish.unitReplenish[tIndex].list6001[port].meterSum);
			INT16U mpIndex = 0;
			for(mpIndex = 0;mpIndex < infoReplenish.unitReplenish[tIndex].list6001[port].meterSum;mpIndex++)
			{
				//DbgPrintToFile1(1,"list6001[%d] = %d",mpIndex,infoReplenish.unitReplenish[tIndex].list6001[port].list6001[mpIndex]);
				if(mpSernum == infoReplenish.unitReplenish[tIndex].list6001[port].list6001[mpIndex])
				{
					if(GetOrSet == 0)
					{
						return infoReplenish.unitReplenish[tIndex].isSuccess[port][mpIndex];
					}
					else
					{
						DbgPrintToFile1(port+1,"任务ID:%d deal6015 测量点 = %d　抄读成功",taskID,mpSernum);
						infoReplenish.unitReplenish[tIndex].isSuccess[port][mpIndex] = 1;
					}

				}
			}
		}
	}
	return ret;
}
/*
 * 处理一个普通采集方案/事件采集方案
 * */
INT8S deal6015or6017(CLASS_6013 st6013,CLASS_6015 st6015, INT8U port485,CLASS_6035* st6035) {
	INT8S result = 0;
	INT16U meterIndex = 0;
	INT16U oi = 0x6000;
	CLASS_6001 meter = { };

	INT8U port = port485-1;

	for (meterIndex = 0; meterIndex < info6000[port].meterSum; meterIndex++)
	{
		DbgPrintToFile1(port485,"1-----------任务ID:%d deal6015 测量点 = %d",st6013.taskID,info6000[port].list6001[meterIndex]);
		memset(&meter,0,sizeof(CLASS_6001));
		if (readParaClass(oi, &meter, info6000[port].list6001[meterIndex]) == 1)
		{
			if (meter.sernum != 0 && meter.sernum != 0xffff)
			{
				if(is485OAD(meter.basicinfo.port,port485)==0)
				{
					continue;
				}
				if (checkMeterType(st6015.mst, meter.basicinfo.usrtype,meter.basicinfo.addr))
				{
						if(getZone("GW")!=0)
						{
							//判断冻结数据是否已经抄读成功了
							if((st6015.csds.csd[0].csd.road.oad.OI == 0x5004)
								&&(GetOrSetFreezeDataSuccess(0,st6013.taskID,port,info6000[port].list6001[meterIndex])==1))
							{
								DbgPrintToFile1(port485,"1-任务ID:%d deal6015 测量点 = %d　已经抄读成功,不用再抄了",st6013.taskID,info6000[port].list6001[meterIndex]);
								continue;
							}
						}
	#if 0
						fprintf(stderr,"\n\n 任务号:%d  方案号:%d deal6015 测量点 = %d-----",st6035->taskID,st6015.sernum,meter.sernum);
						DbgPrintToFile1(port485,"任务号:%d  方案号:%d deal6015 测量点 = %d-----",st6035->taskID,st6015.sernum,meter.sernum);
	#endif

						INT16S dataLen = 0;
						dataLen = deal6015or6017_singlemeter(st6013,st6015,meter,st6035,port485);

						if(dataLen == PARA_CHANGE_RETVALUE)
						{
							DbgPrintToFile1(port485,"参数变更 重新抄表");
							return PARA_CHANGE_RETVALUE;
						}
						if(dataLen > 0)
						{
							if(getZone("GW")!=0)
							{
								//判断冻结数据是否已经抄读成功了
								if(st6015.csds.csd[0].csd.road.oad.OI == 0x5004)
								{
									GetOrSetFreezeDataSuccess(1,st6013.taskID,port,info6000[port].list6001[meterIndex]);
									DbgPrintToFile1(port485,"2-任务ID:%d deal6015 测量点 = %d　已经抄读成功,不用再抄了",st6013.taskID,info6000[port].list6001[meterIndex]);
								}
							}
						}
				}
				else
				{
//					asyslog(LOG_WARNING, "序号=%d的测量点 不是485 %d测量点或者不满足MS条件",info6000[port].list6001[meterIndex],port485);
					DbgPrintToFile1(port485,"序号=%d的测量点 不是485 %d测量点或者不满足MS条件",info6000[port].list6001[meterIndex],port485);
				}
			}
			else
			{
				asyslog(LOG_WARNING, "序号=%d的测量点 档案错误",info6000[port].list6001[meterIndex]);
			}
		}
		else
		{
			 asyslog(LOG_WARNING, "table6000 中不存在 序号=%d的测量点",info6000[port].list6001[meterIndex]);
		}

	}
	return result;
}

INT16S getTaskIndex(INT8U port)
{
	INT16S taskIndex = -1;

	INT8U  rev_485_buf[256];
	INT32S ret;
	mmq_head mq_h;


	switch(port)
	{
		case 2:
		{
			if(mqd_485_2_task < 0)
			{
				return taskIndex;
			}
			ret = mmq_get(mqd_485_2_task, 1, &mq_h, rev_485_buf);
//			fprintf(stderr,"\n\n mqd_485_2_task = %d :mmq_get=%d\n",mqd_485_2_task,ret);
			if(ret > 0)
			{
				//fprintf(stderr,"mqd_485_2_task 接受消息 mq_h.cmd = %d rev_485_buf[0] = %d",mq_h.cmd,rev_485_buf[0]);
				if(mq_h.cmd ==1)
				{
					taskIndex  = rev_485_buf[0];
				}
			}
		}
		break;
		case 31:
		{
			if(mqd_zb_task < 0)
			{
				fprintf(stderr,"mqd_zb_task:mq_open_ret=%d\n",mqd_zb_task);
				return taskIndex;
			}
			ret = mmq_get(mqd_zb_task, 1, &mq_h, rev_485_buf);
			if(ret > 0)
			{
				//fprintf(stderr,"mqd_zb_task 接受消息 mq_h.cmd = %d rev_485_buf[0] = %d",mq_h.cmd,rev_485_buf[0]);
				if(mq_h.cmd ==1)
				{
					taskIndex  = rev_485_buf[0];
				}
			}
		}
		break;
		default:
		{
				if(mqd_485_1_task < 0)
				{
					return taskIndex;
				}
//				fprintf(stderr,"\n\n mqd_485_1_task = %d :mmq_get=%d\n",mqd_485_1_task,ret);
				ret = mmq_get(mqd_485_1_task, 1, &mq_h, rev_485_buf);
				if(ret > 0)
				{
					//fprintf(stderr,"mqd_485_2_task 接受消息 mq_h.cmd = %d rev_485_buf[0] = %d",mq_h.cmd,rev_485_buf[0]);
					if(mq_h.cmd ==1)
					{
						taskIndex  = rev_485_buf[0];
					}
				}
			}

		}
	return taskIndex;
}
INT8S cleanTaskIDmmq(INT8U port485)
{
	INT8S ret = -1;
	INT16S taskIndex = 0;
	do
	{
		taskIndex = getTaskIndex(port485);
		//DbgPrintToFile1(port485,"清理485 %d消息队列中任务index = %d",port485,taskIndex);
	}while(taskIndex > -1);
	para_change485[port485-1] = 0;
	return ret;
}


void read485_thread(void* i485port) {
	INT8U port = *(INT8U*) i485port;
	fprintf(stderr, "\n port = %d", port);


#ifdef TESTDEF1 //这部分是设置新的698表地址用的
	INT8S result = getComfdBy6001(3,2);
	if(result != 1)
	{
		fprintf(stderr,"\n 打开串口失败");
	}
	INT8U jinchang[25] = {0x68,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x68,0x14,0x0D,
						0x34,0x33,0xFF,0x37,0x35,0x33,0x33,0x33,0x44,0x44,
						0x44,0x44,0x44,0xAA,0x16};
	INT8U chgAddr[18]= {0x68,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x68,0x15,0x06,
						0x44,0x55,0x66,0x77,0x88,0x99,0x7F,0x16};

	INT8U requestAddr07[12] ={0x68,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x68,0x13,0x00,0xDF,0x16};

	INT16S RecvLen = -1;
	INT8U RecvBuff[256] = {0};

	SendDataTo485(2, requestAddr07, 12);
	RecvLen = ReceDataFrom485(DLT_645_07,2, 500, RecvBuff);
	//RecvLen = ReceDataFrom485(DLT_698,2, 500, RecvBuff);
	if (RecvLen > 0)
	{
		fprintf(stderr,"\n 进场报文收到回复");
	}
#if 0
	sleep(5);

	memset(RecvBuff,0,256);
	SendDataTo485(2, chgAddr, 18);
	RecvLen = ReceDataFrom485(DLT_698,2, 500, RecvBuff);
	if (RecvLen > 0)
	{
		fprintf(stderr,"\n 修改地址报文收到回复");
	}
#endif
	return;
#endif

	while (1) {

		INT8S ret = dealRealTimeRequst(port);
		if(ret == PARA_CHANGE_RETVALUE)
		{
			cleanTaskIDmmq(port);
		}
		INT16S taskIndex = getTaskIndex(port);

		if((taskIndex > -1)&&(info6000[port-1].meterSum > 0))
		{
			fprintf(stderr,"\n read485_thread ---------port = %d ------ taskIndex = %d \n",port,taskIndex);
			DbgPrintToFile1(port,"******************************************taskIndex = %d 任务开始*******************************",taskIndex);

			JProgramInfo->info6035[taskIndex].taskState = IN_OPR;
			if(JProgramInfo->info6035[taskIndex].sendMsgNum == 0)
			{
				DataTimeGet(&JProgramInfo->info6035[taskIndex].starttime);
			}
			CLASS_6015 to6015;	//采集方案集
			memset(&to6015, 0, sizeof(CLASS_6015));

			switch (list6013[taskIndex].basicInfo.cjtype)
			{
				case norm:/*普通采集方案*/
				case events:/*事件采集方案*/
				{
					ret = use6013find6015or6017(list6013[taskIndex].basicInfo.cjtype,list6013[taskIndex].basicInfo.sernum,list6013[taskIndex].basicInfo.interval,&to6015);
					if(ret == 1)
					{
						ret = deal6015or6017(list6013[taskIndex].basicInfo,to6015,port,&JProgramInfo->info6035[taskIndex]);
					}
					else
					{
						fprintf(stderr,"没找到对应的采集方案");
					}

				}
					break;
				case tran:/*透明采集方案*/
				{

				}
					break;
				case scpt:/*脚本方案*/
				{

				}
					break;
			}
			DataTimeGet(&JProgramInfo->info6035[taskIndex].endtime);
			DbgPrintToFile1(port,"****************taskIndex = %d 任务结束 发送报文数量：%d  接受报文数量：%d*******************************",
					list6013[taskIndex].basicInfo.taskID,JProgramInfo->info6035[taskIndex].sendMsgNum,JProgramInfo->info6035[taskIndex].rcvMsgNum);

			if(getZone("GW")!=0)
			{
				//抄完日冻结任务需要把infoReplenish　保存到文件里　保证重启后补抄不用全部都抄
				//日冻结任务
				if(to6015.csds.csd[0].csd.road.oad.OI == 0x5004)
				{
					//保存需要补抄的数据到文件
					filewrite(REPLENISHFILEPATH,&infoReplenish,sizeof(Replenish_TaskInfo));
					printinfoReplenish(0);
				}
			}

			//判断485故障事件
			if((JProgramInfo->info6035[taskIndex].sendMsgNum > 0)&&(JProgramInfo->info6035[taskIndex].rcvMsgNum==0))
			{
				Event_310A(c485_err,JProgramInfo);
			}
		}
		else
		{

		}

		sleep(1);
	}

	pthread_detach(pthread_self());
	if (port == 1) {
		fprintf(stderr, "485 1 线程退出");
		pthread_exit(&thread_read4851);

	}

	if (port == 2) {
		fprintf(stderr, "485 1 线程退出");
		pthread_exit(&thread_read4852);

	}

	sleep(1);

}
/*
 * 读取CLASS_601F_CFG_FILE "/nor/config/07DI_698OAD.cfg" map07DI_698OAD
 * 将698-07-97 数据标识对应关系放到map07DI_698OAD
 * */
INT8U initMap07DI_698OAD()
{
	map07DI_698OAD_NUM = 0;
	INT8U linenum = 0;
	char aline[MAXLEN_1LINE];
	OI_698 oi698 = 0xffff;
	OI_698 oadOI = 0xffff;
	INT8U unitnum =0,dataType = 0;

	INT8U dataflag97[2];
	INT8U dataflag07[DF07_BYTES*2];
	INT8U dataInfo[DF07_INFO_BYTES];
	int val[4] = {0,0,0,0};
	INT16U flag97 = 0;

	memset(&map07DI_698OAD[0], 0, sizeof(CLASS_601F)*NUM_07DI_698OAD);

	FILE* fp = fopen((const char*)CLASS_601F_CFG_FILE,(const char*)"r");
	if(fp == NULL)
	{
		fprintf(stderr,"\nopen CLASS_601F_CFG_FILE failed!");
		return linenum;
	}
	memset(aline,0,MAXLEN_1LINE);

	while(fgets((char*)aline,MAXLEN_1LINE,fp) != NULL)
	{
		if(index((char*)aline,'#') == (char*)&aline[0])
		{
			memset(aline,0,MAXLEN_1LINE);
			continue;
		}

		if (strstr(aline,"[begin-"))
		{
			int tempvalue;
			sscanf((char*)aline,"[begin-%04x]",&tempvalue);
			oi698 = (OI_698)tempvalue;
			memset(aline,0,MAXLEN_1LINE);
			continue;
		}
		if (strstr(aline,"[end-"))
		{
			oi698 = 0xffff;
			memset(aline,0,MAXLEN_1LINE);
			continue;
		}
		if(oi698==0xffff)
		{
			continue;
		}

		fprintf(stderr,"\n oi698 = %04x\n",oi698);

		memset(dataflag97,0xff,2);
		memset(dataflag07,0xff,DF07_BYTES*2);
		memset(dataInfo,0,DF07_INFO_BYTES);
		flag97 = 0;
		memset(val,0,4);

		fprintf(stderr,"\n------------------------");
		sscanf((const char*)aline,"%04x %04x %s %d %d %s",&val[0],&val[1],dataflag07,&val[2],&val[3],dataInfo);
		fprintf(stderr,"\n aline = %s ------------------------",aline);
		oadOI = (OI_698)val[0];
		flag97 = (INT16U)val[1];
		memcpy(dataflag97,&flag97,2);

		unitnum = (INT8U)val[2];
		dataType = (INT8U)val[3];

		INT8U asc07[DF07_BYTES*2];
		memset(asc07,0,DF07_BYTES*2);
		memcpy(asc07,dataflag07,DF07_BYTES*2);

		INT8U buf07[DF07_BYTES*2];
		memset(buf07,0,DF07_BYTES*2);
		asc2bcd(asc07,DF07_BYTES*2,buf07,positive);
		reversebuff(buf07, 4, dataflag07);


		fprintf(stderr,"\n linenum = %d  oadOI=%04x  datatype = %d unitnum= %d	dataflag97 = %02x%02x dataflag07 = %02x-%02x-%02x-%02x dataInfo = %s\n"
						,linenum,oadOI,dataType,unitnum,dataflag97[0],dataflag97[1],dataflag07[0],dataflag07[1],dataflag07[2],dataflag07[3],dataInfo);

		map07DI_698OAD[linenum].roadOI = oi698;
		map07DI_698OAD[linenum].flag698.OI = oadOI;
		map07DI_698OAD[linenum].flag698.attflg = 0x02;
		map07DI_698OAD[linenum].flag698.attrindex = 0x00;
		map07DI_698OAD[linenum].unitnum = unitnum;
		map07DI_698OAD[linenum].datatype = dataType;
		map07DI_698OAD[linenum].flag97.dinum =1;
		memcpy(map07DI_698OAD[linenum].flag97.DI_1,dataflag97,2);
		//97表需量和需量时间分两个数据项
		if((map07DI_698OAD[linenum].flag97.DI_1[0][1]==0xA0)||(map07DI_698OAD[linenum].flag97.DI_1[0][1]==0xA1))
		{
			map07DI_698OAD[linenum].flag97.dinum = 2;
			map07DI_698OAD[linenum].flag97.DI_1[1][1] = map07DI_698OAD[linenum].flag97.DI_1[0][1] + 0x10;
			map07DI_698OAD[linenum].flag97.DI_1[1][0] = map07DI_698OAD[linenum].flag97.DI_1[0][0];
		}
		map07DI_698OAD[linenum].flag07.dinum =1;
		memcpy(map07DI_698OAD[linenum].flag07.DI_1,dataflag07,DF07_BYTES);
		linenum++;
		memset(aline,0,MAXLEN_1LINE);


		if(linenum >= NUM_07DI_698OAD)
		{
			fprintf(stderr,"\n 07DI_698OAD.cfg数据项超过 NUM_07DI_698OAD \n");
			fclose(fp);
			return linenum;
		}

	}

	map07DI_698OAD[linenum].roadOI = 0x0000;
	map07DI_698OAD[linenum].flag698.OI = 0x4000;
	map07DI_698OAD[linenum].flag698.attflg = 0x02;
	map07DI_698OAD[linenum].flag698.attrindex = 0x00;
	map07DI_698OAD[linenum].unitnum = 1;
	map07DI_698OAD[linenum].datatype = 0;
	map07DI_698OAD[linenum].flag97.dinum = 2;
	map07DI_698OAD[linenum].flag97.DI_1[0][0] = 0x10;
	map07DI_698OAD[linenum].flag97.DI_1[0][1] = 0xc0;
	map07DI_698OAD[linenum].flag97.DI_1[1][0] = 0x11;
	map07DI_698OAD[linenum].flag97.DI_1[1][1] = 0xc0;
	map07DI_698OAD[linenum].flag07.dinum = 2;
	memcpy(map07DI_698OAD[linenum].flag07.DI_1[0],flag07_date,DF07_BYTES);
	memcpy(map07DI_698OAD[linenum].flag07.DI_1[1],flag07_time,DF07_BYTES);
	linenum++;

	if (linenum < 0)
	{
		fprintf(stderr,"\nLoad  07DI_698OAD.cfg Error!!!     lineNum=%d\n", linenum);
	}
	else
	{
		fprintf(stderr,"\nLoad  07DI_698OAD.cfg  OK!!!        lineNum=%d\n", linenum);
	}
	fclose(fp);
	return linenum;
}

void read485_proccess() {

	i485port1 = S4851;
	i485port2 = S4852;
	//comfd-存放打开串口fd
	comfd485[0] = -1;
	comfd485[1] = -1;
	readState = 0;
	isNeed4852 = 1;
	if((getZone("HuNan") == 0)&&((JProgramInfo->cfg_para.device == CCTT1)||(JProgramInfo->cfg_para.device == SPTF3)))
	{
		isNeed4852 = 0;
		CLASS_f201 oif201[3] = {};
		if(readCoverClass(0xf201, 0, oif201, sizeof(CLASS_f201)*3, para_vari_save)==1)
		{
			if(oif201[1].devfunc ==1)
			{
				isNeed4852 = 1;
				asyslog(LOG_INFO,"485-2抄表");
			}
		}
	}


	struct mq_attr attr_485_1_task;
	mqd_485_1_task = mmq_open((INT8S *)TASKID_485_1_MQ_NAME,&attr_485_1_task,O_RDONLY);

	struct mq_attr attr_485_2_task;
	mqd_485_2_task = mmq_open((INT8S *)TASKID_485_2_MQ_NAME,&attr_485_2_task,O_RDONLY);



	pthread_attr_init(&read485_attr_t);
	pthread_attr_setstacksize(&read485_attr_t, 2048 * 1024);
	pthread_attr_setdetachstate(&read485_attr_t, PTHREAD_CREATE_DETACHED);

	while ((thread_read4851_id = pthread_create(&thread_read4851,&read485_attr_t, (void*) read485_thread, &i485port1)) != 0)
	{
		sleep(1);
	}

	if(isNeed4852 == 1)
	{
		while ((thread_read4852_id=pthread_create(&thread_read4852, &read485_attr_t, (void*)read485_thread, &i485port2)) != 0)
		{
			sleep(1);
		}
	}


}
void read485QuitProcess()
{
	mmq_close(mqd_485_main);
	mmq_close(mqd_485_1_task);
	mmq_close(mqd_485_2_task);
	pthread_attr_destroy(&read485_attr_t);
}
