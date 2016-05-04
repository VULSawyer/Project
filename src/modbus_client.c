/*Modified modbus client sample from libmodbus.org*/
/*added error checking to basic sample with messaging
* changed context variable to ctx 
* port number is 502 (default) and user must be SU to access that part of memory
*/

#include <stdio.h>
#include <modbus_tcp.h>

int main(void) {

	modbus_t *ctx;
	uint16_t tab_reg[32]

	ctx = modbus_new_tcp("127.0.0.1", 502);  //port requires admin privileges
	//error checking
	if (ctx == NULL)  {
		fprintf(stderr, "Unable to allocate libmodbus context\n");
		return -1;
	}

	modbus_connect(ctx);
	//error checking
        if (modbus_connect(ctx) == -1) {
		fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		return -1;
	
	}

	else {
		printf("Connected to server\n");
	}

/* Read 5 registers from the address 0 */
	modbus_read_registers(ctx, 0, 5, tab_reg);

	printf("Register value:%04x\n", tab_reg[0]);

	modbus_close(ctx);
	modbus_free(ctx);
}
