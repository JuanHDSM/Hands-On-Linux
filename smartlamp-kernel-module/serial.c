#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102)");
MODULE_LICENSE("GPL");

#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositivo USB

static struct usb_device *smartlamp_device;        // Referência para o dispositivo USB
static uint usb_in, usb_out;                       // Endereços das portas de entrada e saída da USB
static char *usb_in_buffer, *usb_out_buffer;       // Buffers de entrada e saída da USB
static int usb_max_size;                           // Tamanho máximo de uma mensagem USB

#define VENDOR_ID   0x10c4
#define PRODUCT_ID  0xea60
static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };
MODULE_DEVICE_TABLE(usb, id_table);

bool ignore = true;
int LDR_value = 0;

// Funções principais
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void usb_disconnect(struct usb_interface *interface);
static int usb_read_serial(void);

// Driver USB
static struct usb_driver smartlamp_driver = {
    .name        = "smartlamp",
    .probe       = usb_probe,
    .disconnect  = usb_disconnect,
    .id_table    = id_table,
};

module_usb_driver(smartlamp_driver);

// Executado quando o dispositivo USB é conectado
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *usb_endpoint_in, *usb_endpoint_out;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    // Detecta portas e aloca buffers de entrada e saída de dados na USB
    smartlamp_device = interface_to_usbdev(interface);
    ignore = usb_find_common_endpoints(interface->cur_altsetting,
                                       &usb_endpoint_in, &usb_endpoint_out,
                                       NULL, NULL);

    usb_max_size = usb_endpoint_maxp(usb_endpoint_in);
    usb_in = usb_endpoint_in->bEndpointAddress;
    usb_out = usb_endpoint_out->bEndpointAddress;

    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    LDR_value = usb_read_serial();
    printk(KERN_INFO "SmartLamp: Valor LDR: %d\n", LDR_value);

    return 0;
}

// Executado quando o dispositivo USB é desconectado
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    kfree(usb_in_buffer);
    kfree(usb_out_buffer);
}

// Função de leitura da resposta da serial do ESP32
static int usb_read_serial(void) {
    int ret, actual_size;
    int retries = 10;
    const char *prefix = "RES GET_LDR ";
    int value = 0;
    int i;
    char *start;

    while (retries > 0) {
        ret = usb_bulk_msg(smartlamp_device,
                           usb_rcvbulkpipe(smartlamp_device, usb_in),
                           usb_in_buffer,
                           min(usb_max_size, MAX_RECV_LINE),
                           &actual_size,
                           1000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d). Código: %d\n", retries, ret);
            retries--;
            continue;
        }

        if (actual_size > 0) {
            usb_in_buffer[actual_size] = '\0';
            printk(KERN_INFO "SmartLamp: Dados recebidos: %s\n", usb_in_buffer);

            // Debug: mostrar cada byte recebido
            for (i = 0; i < actual_size; i++) {
                printk(KERN_INFO "Byte %d: 0x%02x (%c)\n", i, usb_in_buffer[i], usb_in_buffer[i]);
            }

            start = strstr(usb_in_buffer, prefix);
            if (start) {
                if (sscanf(start + strlen(prefix), "%d", &value) == 1) {
                    printk(KERN_INFO "SmartLamp: Valor LDR extraído: %d\n", value);
                    return value;
                }
            } else {
                printk(KERN_ERR "SmartLamp: Resposta inesperada: %s\n", usb_in_buffer);
            }

            retries--;
            ssleep(1); // Espera breve entre tentativas
        }
    }

    return -1;
}

// Função reserva (não usada atualmente)
static int __maybe_unused usb_read_serial_bkp(void) {
    int ret, actual_size;
    int retries = 10;
    const char *prefix = "RES GET_LDR ";
    int value = 0;
    int i;
    char *start;

    while (retries > 0) {
        ret = usb_bulk_msg(smartlamp_device,
                           usb_rcvbulkpipe(smartlamp_device, usb_in),
                           usb_in_buffer,
                           min(usb_max_size, MAX_RECV_LINE),
                           &actual_size,
                           1000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d). Código: %d\n", retries, ret);
            retries--;
            continue;
        }

        if (actual_size > 0) {
            usb_in_buffer[actual_size] = '\0';
            printk(KERN_INFO "SmartLamp: Dados recebidos: %s\n", usb_in_buffer);

            for (i = 0; i < actual_size; i++) {
                printk(KERN_INFO "Byte %d: 0x%02x (%c)\n", i, usb_in_buffer[i], usb_in_buffer[i]);
            }

            start = strstr(usb_in_buffer, prefix);
            if (start) {
                if (sscanf(start + strlen(prefix), "%d", &value) == 1) {
                    printk(KERN_INFO "SmartLamp: Valor LDR extraído: %d\n", value);
                    return value;
                }
            } else {
                printk(KERN_ERR "SmartLamp: Resposta inesperada: %s\n", usb_in_buffer);
            }

            retries--;
            ssleep(1);
        }
    }

    return -1;
}
