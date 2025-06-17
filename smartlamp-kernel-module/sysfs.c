#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/string.h>


MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102");
MODULE_LICENSE("GPL");


#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositvo USB
#define SMARTLAMP_INTERFACE 1

static struct usb_device *smartlamp_device;        // Referência para o dispositivo USB
static uint usb_in, usb_out;                       // Endereços das portas de entrada e saida da USB
static char *usb_in_buffer, *usb_out_buffer;       // Buffers de entrada e saída da USB
static int usb_max_size;                           // Tamanho máximo de uma mensagem USB

#define VENDOR_ID   0x1a86
#define PRODUCT_ID  0x55d4

static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };

static int  usb_probe(struct usb_interface *ifce, const struct usb_device_id *id); // Executado quando o dispositivo é conectado na USB
static void usb_disconnect(struct usb_interface *ifce);                           // Executado quando o dispositivo USB é desconectado da USB
static int  usb_read_serial(const char *prefix);   

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff);
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count);   

// Variáveis para criar os arquivos no /sys/kernel/smartlamp/{led, ldr}
static struct kobj_attribute  led_attribute = __ATTR(led, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  ldr_attribute = __ATTR(ldr, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct attribute      *attrs[]       = { &led_attribute.attr, &ldr_attribute.attr, NULL };
static struct attribute_group attr_group    = { .attrs = attrs };
static struct kobject        *sys_obj;                                             // Executado para ler a saida da porta serial

MODULE_DEVICE_TABLE(usb, id_table);

bool ignore = true;
int LDR_value = 0;

static struct usb_driver smartlamp_driver = {
    .name        = "smartlamp",     // Nome do driver
    .probe       = usb_probe,       // Executado quando o dispositivo é conectado na USB
    .disconnect  = usb_disconnect,  // Executado quando o dispositivo é desconectado na USB
    .id_table    = id_table,        // Tabela com o VendorID e ProductID do dispositivo
};

module_usb_driver(smartlamp_driver);

// Executado quando o dispositivo é conectado na USB
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *usb_endpoint_in, *usb_endpoint_out;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    // Cria arquivos do /sys/kernel/smartlamp/*
    sys_obj = kobject_create_and_add("smartlamp", kernel_kobj);
    ignore = sysfs_create_group(sys_obj, &attr_group); // AQUI

    // Detecta portas e aloca buffers de entrada e saída de dados na USB
  struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    if (!interface || !interface->cur_altsetting) {
        printk(KERN_ERR "SmartLamp: Interface ou altsetting inválidos\n");
        return -ENODEV;
    }

    iface_desc = interface->cur_altsetting;

    // Verifica se é a interface esperada
    if (iface_desc->desc.bInterfaceNumber != SMARTLAMP_INTERFACE) {
        printk(KERN_INFO "SmartLamp: Ignorando interface %d (esperado: %d).\n",
               iface_desc->desc.bInterfaceNumber, SMARTLAMP_INTERFACE);
        return -ENODEV;
    }

    printk(KERN_INFO "SmartLamp: Número de endpoints: %d\n", iface_desc->desc.bNumEndpoints);

    usb_in = 0;
    usb_out = 0;

    // Busca endpoints bulk IN e OUT
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;
        printk(KERN_INFO "SmartLamp: Endpoint[%d]: addr=0x%02x, attr=0x%02x\n",
               i, endpoint->bEndpointAddress, endpoint->bmAttributes);

        if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
            if (endpoint->bEndpointAddress & USB_DIR_IN)
                usb_in = endpoint->bEndpointAddress;
            else
                usb_out = endpoint->bEndpointAddress;
        }
    }

    if (!usb_in || !usb_out) {
        printk(KERN_ERR "SmartLamp: Endpoints Bulk IN/OUT não encontrados. Dispositivo não suportado.\n");
        return -ENODEV;
    }

    smartlamp_device = interface_to_usbdev(interface);

    // Obtém o tamanho máximo do pacote do endpoint IN
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;
        if (endpoint->bEndpointAddress == usb_in) {
            usb_max_size = usb_endpoint_maxp(endpoint);
            break;
        }
    }

    if (!usb_max_size) {
        printk(KERN_ERR "SmartLamp: Falha ao obter tamanho do pacote do endpoint IN.\n");
        return -ENODEV;
    }

    // Aloca buffers com espaço extra para o terminador nulo
    usb_in_buffer = kmalloc(usb_max_size + 1, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    if (!usb_in_buffer || !usb_out_buffer) {
        printk(KERN_ERR "SmartLamp: Falha na alocação de buffers.\n");
        kfree(usb_in_buffer);
        usb_in_buffer = NULL;
        kfree(usb_out_buffer);
        usb_out_buffer = NULL;
        return -ENOMEM;
    }

    printk(KERN_INFO "SmartLamp: Endpoint IN: 0x%02x, OUT: 0x%02x, tamanho: %d\n",
           usb_in, usb_out, usb_max_size);

    LDR_value = usb_read_serial("RES GET_LDR ");
    printk(KERN_INFO "SmartLamp: Valor LDR: %d\n", LDR_value);


    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    if (sys_obj) kobject_put(sys_obj);      // Remove os arquivos em /sys/kernel/smartlamp
    kfree(usb_in_buffer);                   // Desaloca buffers
    kfree(usb_out_buffer);
}

static int usb_read_serial(const char *prefix) {
    int ret, actual_size;
    int retries = 20;
    int value = 0;
    char *line;
    char *saveptr;
    char *buffer_copy;

    while (retries > 0) {
        ret = usb_bulk_msg(smartlamp_device,
                           usb_rcvbulkpipe(smartlamp_device, usb_in),
                           usb_in_buffer,
                           min(usb_max_size, MAX_RECV_LINE),
                           &actual_size,
                           1000);

        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d). Codigo: %d\n", retries, ret);
            retries--;
            continue;
        }

        if (actual_size > 0) {
            usb_in_buffer[actual_size] = '\0';
            printk(KERN_INFO "SmartLamp: Dados recebidos do dispositivo:\n%s\n", usb_in_buffer);

            // Precisamos de uma cópia para usar com strsep(), pois ela modifica a string
            buffer_copy = usb_in_buffer;
            while ((line = strsep(&buffer_copy, "\n")) != NULL) {
                printk(KERN_INFO "SmartLamp: Linha processada: %s\n", line);

                if (strncmp(line, prefix, strlen(prefix)) == 0) {
                    if (sscanf(line + strlen(prefix), "%d", &value) == 1) {
                        printk(KERN_INFO "SmartLamp: Valor extraído: %d\n", value);
                        return value;
                    } else {
                        printk(KERN_ERR "SmartLamp: Prefixo encontrado, mas falha ao extrair valor: %s\n", line);
                    }
                } else {
                    printk(KERN_ERR "SmartLamp: Linha ignorada (prefixo não bate): %s\n", line);
                }
            }

            retries--;
            ssleep(1);
        }
    }

    printk(KERN_ERR "SmartLamp: Falha ao obter resposta válida após múltiplas tentativas.\n");
    return -1;
}


// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff) {
    // value representa o valor do led ou ldr
    int value = -1;
    // attr_name representa o nome do arquivo que está sendo lido (ldr ou led)
    const char *attr_name = attr->attr.name;

    // printk indicando qual arquivo está sendo lido
    printk(KERN_INFO "SmartLamp: Lendo %s ...\n", attr_name);

    // Implemente a leitura do valor do led usando a função usb_read_serial()
    if (strncmp(attr_name, "led",3) == 0) {
        value = usb_read_serial("RES GET_LED ");
    }

    sprintf(buff, "%d\n", value);                   // Cria a mensagem com o valor do led, ldr
    return strlen(buff);
}


// Essa função não deve ser alterada durante a task sysfs
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count) {
    long ret, value;
    const char *attr_name = attr->attr.name;

    // Converte o valor recebido para long
    ret = kstrtol(buff, 10, &value);
    if (ret) {
        printk(KERN_ALERT "SmartLamp: valor de %s invalido.\n", attr_name);
        return -EACCES;
    }

    printk(KERN_INFO "SmartLamp: Setando %s para %ld ...\n", attr_name, value);

    if (ret < 0) {
        printk(KERN_ALERT "SmartLamp: erro ao setar o valor do %s.\n", attr_name);
        return -EACCES;
    }

    return strlen(buff);
}