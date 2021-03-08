/****************************************************************************
 *                                                                          *
 * arcwatch - log and mail events of your Areca Raid Controller             *
 * Copyright (C) 2006 Volker Gropp (mail@gropp.org)                         *
 *                                                                          *
 * Version 0.1 (2006-07-16) initial Version with basic features             *
 *                                                                          *
 * to build:                                                                *
 * g++ -static -I Arclib/include -o arcwatch arcwatch.cpp \                 *
 *      Arclib/linux/x86-64/release/arclib64.a -lpthread -Wall              *
 *                                                                          *
 * This program is free software; you can redistribute it and/or modify     *
 * it under the terms of the GNU General Public License as published by     *
 * the Free Software Foundation, version 2.                                 *
 *                                                                          *
 * This program is distributed in the hope that it will be useful,          *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 * GNU General Public License for more details.                             *
 *                                                                          *
 * You should have received a copy of the GNU General Public License        *
 * along with this program; if not, write to the Free Software              *
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA  *
 *                                                                          *
 ***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>
#include <getopt.h>

/*
 * ARCLIB stuff 
 */
#include <arclib.h>
#include <ethernet.h>
#include <linux_comm.h>
#include <linux_ioctl.h>
#include <linux_scsi.h>


#define MAX_CONTROLLER_SUPPORTED 4
CArclib ar[MAX_CONTROLLER_SUPPORTED];



#define SENDMAIL "/usr/sbin/sendmail"
#define RECIPIENTS "root"
#define FROM "ARC RAID LOG-ENGINE (arcwatch) <root>"
#define MAIL_SUBJECT "ARC RAID ERROR!"
#define SYSLOG_LVL LOG_CRIT



struct st_config {
	char background;
	char printall;
	char from[255];
	char recipients[255];
	char mail_subject[255];
};

struct st_config config = {0,0,FROM,RECIPIENTS,MAIL_SUBJECT};


int send_mail(char* buf);
int put_log(int level, char *msg);

void sig_int(int signr) {
	if (config.background) {	
		put_log(LOG_NOTICE,"Shutdown requested!");
		exit(0);
	}
}


/************************************************************************************
    Routine Name: DiscoveryCommDevice
    Function:     Discovery the controller(s) connected to the RS-232 port
    Input:        None.
    Output:       None.
    Return Values:   1. Total controller(s) found.
    Note:         None.
************************************************************************************/
int
DiscoveryCommDevice()
{
	int  nTotalCtrl = 0;

	LinuxCommInterface *iface = new LinuxCommInterface();
	for (int i = 0; i < 4; i++) {
		if (iface->init(i)) {
			ar[nTotalCtrl++].ArcInitSession(iface);
			if (nTotalCtrl >= MAX_CONTROLLER_SUPPORTED) {
				break;
			}

			iface = new LinuxCommInterface();
		}
	}

	return nTotalCtrl;
}


/************************************************************************************
    Routine Name: DiscoveryCardDevice
    Function:     Discovery the controller(s) connected to the PCI, PCI-X, PCI-E bus
    Input:        None.
    Output:       None.
    Return Values:   1. Total controller(s) found
    Note:         None.
************************************************************************************/
int
DiscoveryCardDevice()
{
	const char areca_driver_name[] = "arcmsr";
	int  major_num = 0;
	int  minor_num = 0;

	char major_num_str[4];
	char dev_name[33];
	struct stat stat_of_dev;
	int  controller_index = 0;
	mode_t mode = 0666;

	int  nTotalCtrl = 0;

	FILE *devices_file = NULL;

	devices_file = fopen("/proc/devices", "r");
	if (!devices_file) {
		return 0;
	}

	// get areca's major#
	while (fscanf(devices_file, "%3s%32s", major_num_str, dev_name) != EOF) {
		major_num_str[sizeof(major_num_str) - 1] = 0;
		dev_name[sizeof(dev_name) - 1] = 0;

		// find areca's major#
		if (!memcmp(areca_driver_name, dev_name, 6)) {
			major_num = atoi(major_num_str);
			break;
		}
	}

	fclose(devices_file);

	if (!major_num) {
		return 0;
	}

	mode |= S_IFCHR;

	for (minor_num = 0; minor_num < MAX_CONTROLLER_SUPPORTED; minor_num++) {
		sprintf(dev_name, "/dev/arcmsr%d", minor_num);
		if (!stat(dev_name, &stat_of_dev)) {
			// device name found, check to see if the major and minor were 
			// correct.
			if (major_num != (int) major(stat_of_dev.st_rdev) ||
				minor_num != (int) minor(stat_of_dev.st_rdev)) {
				// major and minor number are not match, delete the old
				// device name
				unlink(dev_name);
			} else {
				continue;
			}
		}

		if (mknod(dev_name, mode, makedev(major_num, minor_num))) {
			return 0;
		}
	}

	// setup areca controller(s)
	for (controller_index = 0; controller_index < MAX_CONTROLLER_SUPPORTED;
		 controller_index++) {
		LinuxIoctlInterface *iface = new LinuxIoctlInterface();
		if (iface) {
			if (iface->init(controller_index)) {
				// call ArcInitSession(...) member function first before
				// any others member functions get call
				ar[controller_index].ArcInitSession(iface);

				// save the total controller(s) we found
				nTotalCtrl++;
			} else {
				delete iface;
				iface = NULL;
			}					// if
		}						// if
	}							// for

	return nTotalCtrl;

}


/************************************************************************************
    Routine Name: DiscoveryInbandDevice
    Function:     Discovery the controller(s) connected to the Inband scsi bus
    Input:        None.
    Output:       None.
    Return Values:   1. Total controller(s) found
    Note:         None.
************************************************************************************/
int
DiscoveryInbandDevice()
{
	int  sg = 0;
	char stat;
	int  nTotalCtrl = 0;

	do {
		LinuxSCSIInterface *iface = new LinuxSCSIInterface();

		for (int retry = 0; retry < 5; retry++) {
			stat = (int) iface->init(sg);
			if (stat)
				break;
		}

		if (stat) {
			ar[nTotalCtrl++].ArcInitSession(iface);
			break;
		} else {
			delete iface;
		}
	}
	while (++sg <= 15 && nTotalCtrl < MAX_CONTROLLER_SUPPORTED);

	return nTotalCtrl;
}


int
getEventCount(CArclib & ctrl, int flags)
{
	int  count;
	ARC_STATUS stat = ARC_SUCCESS;
	stat = ctrl.ArcGetReqEventPage(flags, &count);
	if (stat != ARC_SUCCESS) {
		return 0;
	}
	return count;
}


ARC_STATUS
getEvent(CArclib & ctrl, int index, char *buf, char *timeBuf)
{
	ARC_STATUS stat = ARC_SUCCESS;
	struct tm tm;
	sSYSTEM_INFO sysInfo;
	pEVENT_DATA pEventObject = (pEVENT_DATA) new sEVENT_DATA;
	pSYS_TIME evtTime = (pSYS_TIME) new sSYS_TIME;

	memset(pEventObject, 0, sizeof(sEVENT_DATA));

	if (ctrl.ArcGetEventObject(index, pEventObject)) {
		*((LONG *) evtTime) = pEventObject->evtTime;

		if (pEventObject->evtTime & 0x80000000) {
			snprintf(timeBuf, 64, "%4d-%d-%d  %d:%d:%d",
					 evtTime->u.tmYear + 2000,
					 evtTime->u.tmMonth,
					 evtTime->u.tmDate,
					 evtTime->u.tmHour,
					 evtTime->u.tmMinute, evtTime->u.tmSecond);
			timeBuf[63] = 0;
			if (!strptime(timeBuf, "%Y-%m-%d %H:%M:%S", &tm))
				return ARC_SUCCESS;
			strftime(timeBuf, 64, "%a, %d %b %Y %H:%M:%S", &tm);
		} else {
			snprintf(timeBuf, 64, "%d", evtTime->x.tmTick);
		}
		timeBuf[63] = 0;
		/*
		 * Parse the event object we got 
		 */
		switch ((int) pEventObject->evtCategory) {
			/*
			 * The member "evtStr" of event object structure contains the
			 * string to indicate which raidset owns this event object 
			 */
		case EVENT_RAIDSET:	// raidset event
			snprintf(buf, 128, "%-20s%s", pEventObject->evtStr,
					 htmRaidEvent[pEventObject->evtType]);
			break;

			/*
			 * The member "evtStr" of event object structure contains the
			 * string to indicate which volumeset owns this event object 
			 */
		case EVENT_VOLUMESET:	// volumeset event
			snprintf(buf, 128, "%-20s%s", pEventObject->evtStr,
					 htmVolEvent[pEventObject->evtType]);
			break;

		case EVENT_DEVICE:		// device event
			snprintf(buf, 128, "IDE Channel #%2d     %s",
					 pEventObject->evtChannel + 1,
					 htmDevEvent[pEventObject->evtType]);
			break;

		case EVENT_HOST:		// host event
			/*
			 * this is Host SCSI events 
			 */
			stat = ctrl.ArcGetSysInfo(&sysInfo);
			if (stat != ARC_SUCCESS)
				return stat;
			if (sysInfo.gsiScsiHostChannels) {
				snprintf(buf, 128, "%-20s%s",
						 eventCat[pEventObject->evtCategory],
						 htmScsiHostEvent[pEventObject->evtType]);
			} else {
				snprintf(buf, 128, "%-20s%s",
						 eventCat[pEventObject->evtCategory],
						 htmIdeHostEvent[pEventObject->evtType]);
			}
			break;

		case EVENT_HW_MONITOR:	// hardware monitor event
			snprintf(buf, 128, "%-20s%s",
					 eventCat[pEventObject->evtCategory],
					 htmHwMonEvent[pEventObject->evtType]);
			break;
		case EVENT_NEW_83782D:
			snprintf(buf, 128, "%-20s%s", pEventObject->evtStr,
					 htmNewEventStr[pEventObject->evtType]);
			break;

		case EVENT_NO_EVENT:
		default:
			snprintf(buf, 128, "No or undefined Event");
			return stat;
			break;

		}						// end switch
		buf[127] = 0;
	}
	if (pEventObject)
		delete pEventObject;
	if (evtTime)
		delete evtTime;
	return stat;
}


void
printAllEvents(CArclib & ctrl)
{
	char buf[128];
	int  nCount = 0;
	int  index;
	char timeBuf[63];

	nCount = getEventCount(ctrl, FLAG_ALL);
	if (nCount != 0) {
		printf("Date-Time                Device              Event Type        \n");
		printf("================================================================================\n");
		for (index = nCount - 1; index >=0; index--) {
			if (ARC_SUCCESS == getEvent(ctrl, index, buf, timeBuf)) {
				printf("%s %s\n", timeBuf, buf);
				send_mail(buf);
			}
		}	
		printf("================================================================================\n");
	}	
	return;
}

int
send_mail(char* buf)
{
	FILE *smail;

	smail = popen(SENDMAIL " -ti", "w");
	if (!smail)
		return 0;
	fprintf(smail,"To: %s\n",config.recipients);
	fprintf(smail,"From: %s\n",config.from);
	fprintf(smail,"Subject: %s\n\n",config.mail_subject);
	fprintf(smail,"%s",buf);
	pclose(smail);
	return 1;
}

int
put_log(int level, char *msg)
{
	syslog(level, "%s", msg);
	return 0;
}



void
print_help() {
}

void
get_cli_option(int argc, char *argv[]) {
	int option_index = 0;
	int o = 0;
	static struct option long_options[] = {
		{"printall", 0, 0, 'p'},
		{"daemonize",0,0, 'd'},
		{"help",0,0,'h'},
		{0,0,0,0}
	};
	while (1) {
		o=getopt_long (argc,argv,"pdh",long_options, &option_index);
		switch (o) {
			case -1 :
				return;
			case '?': 
				printf("unknown option: %s\n",argv[optind-1]);
				exit(EXIT_FAILURE);
				break;
			case 'p':
				config.printall=1;
				break;
			case 'd':
				config.background=1;
				break;
			case 'h':
				print_help();
				exit(EXIT_SUCCESS);
				break;
		}	
	}				  
}


void
getNewEvents(int total_ctrl) {
	int i;
	int n;
	int count;
	char buf[128];
	char timeBuf[64];
	
	for (i = 0; i < total_ctrl; i++) {
		count = getEventCount(ar[i], FLAG_NEW);	
		for (n=0;n<count;n++) {
			if (ARC_SUCCESS == getEvent(ar[i], n, buf, timeBuf)) {
				put_log(SYSLOG_LVL,buf);	
				send_mail(buf);
			}
		}
	}
}

int
main(int argc, char **argv)
{
	int		total_ctrl = 0;
	int		i;
	int		nbyt;

	get_cli_option(argc,argv);

	// lookup controllers
	total_ctrl = DiscoveryCardDevice();
	if (!total_ctrl) {
		printf("No Controller found!\n");
		return 1;
	}

	
	if (config.printall) {
		for (i = 0; i < total_ctrl; i++) {
			// set correct time for next events
			ar[i].ArcSetTime();
			printAllEvents(ar[i]);
		}
		exit(EXIT_SUCCESS);
	}

	// init FLAG_NEW, so we only get new events on next call
	for (i = 0; i < total_ctrl; i++) {
		ar[i].ArcSetTime();
		getEventCount(ar[i], FLAG_NEW);
	}
		

	/*
	 * lets fork into background 
	 */
	
	if (config.background) { 
		if ((nbyt = fork()) == -1) { 
			printf("could not fork into background"); 
			return -1; 
		} 
		if (nbyt != 0) {
			printf("forking into background (%i).\n",nbyt); 
			return 0; 
		}
		setsid(); 
	} 
	openlog("arcwatch",LOG_ODELAY,LOG_DAEMON); 
	put_log(LOG_NOTICE,"Startup successfull! Waiting for new events now...");

	/* set up some signal handlers */
	signal(SIGINT,sig_int);
	signal(SIGTERM,sig_int);

	while (1) {
		getNewEvents(total_ctrl);
		sleep(10);
	}
	put_log(LOG_NOTICE,"Shutdown requested!");
	return 0; 
}
