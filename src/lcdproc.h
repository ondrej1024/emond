/* 
 * Energy Monitor: build-in lcdproc client
 * 
 * Author: Ondrej Wisniewski
 * 
 */

#ifndef __LCDPROC_H__
#define __LCDPROC_H__

/**********************************************************
 * Public function: lcd_init()
 * 
 * Description:
 *           Perform the initialization and configuration
 *           of the LCD display
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int lcd_init(void);

/**********************************************************
 * Public function: lcd_exit()
 * 
 * Description:
 *           Close the connection to the LCDd daemon
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int lcd_exit(void);

/**********************************************************
 * Public function: lcd_print()
 * 
 * Description:
 *           Print data on the LCD display
 * 
 * Returns:  0 on success, <0 otherwise
 *********************************************************/
int lcd_print(int line, unsigned int value);

#endif /* __LCDPROC_H__ */ 
