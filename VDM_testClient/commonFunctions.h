/*
 * commonFunctions.h
 *
 *  Created on: Apr 16, 2015
 *      Author: keinsword
 */

#ifndef COMMONFUNCTIONS_H_
#define COMMONFUNCTIONS_H_

void handleErr(short errCode);

int fdSetBlocking(int fd, int blocking);

void strToLower(char *str);

int checkArgs(char *port, char *transport);

#endif /* COMMONFUNCTIONS_H_ */
