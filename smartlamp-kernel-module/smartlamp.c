#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102");
MODULE_LICENSE("GPL");


#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositvo USB


static char recv_line[MAX_RECV_LINE];              // Armazena dados vindos da USB até receber um caractere de nova linha '\n'
static struct usb_device *smartlamp_device;        // Referência para o dispositivo USB
static uint usb_in, usb_out;                       // Endereços das portas de entrada e saida da USB
static char *usb_in_buffer, *usb_out_buffer;       // Buffers de entrada e saída da USB
static int usb_max_size;                           // Tamanho máximo de uma mensagem USB

#define VENDOR_ID   0x1a86
#define PRODUCT_ID  0x55d4

static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };

static int  usb_probe(struct usb_interface *ifce, const struct usb_device_id *id); // Executado quando o dispositivo é conectado na USB
static void usb_disconnect(struct usb_interface *ifce);                           // Executado quando o dispositivo USB é desconectado da USB
static int  usb_read_serial(void);   
static int  usb_send_cmd(char *cmd, int param);

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff);
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count);   
// Variáveis para criar os arquivos no /sys/kernel/smartlamp/{led, ldr}
static struct kobj_attribute  led_attribute = __ATTR(led, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  ldr_attribute = __ATTR(ldr, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute temp_attribute = __ATTR(temp, S_IRUGO, attr_show, NULL);
static struct kobj_attribute hum_attribute  = __ATTR(hum,  S_IRUGO, attr_show, NULL);



static struct attribute *attrs[] = {
    &led_attribute.attr,
    &ldr_attribute.attr,
    &temp_attribute.attr,
    &hum_attribute.attr,
    NULL
};

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
    smartlamp_device = interface_to_usbdev(interface);
    ignore =  usb_find_common_endpoints(interface->cur_altsetting, &usb_endpoint_in, &usb_endpoint_out, NULL, NULL);  // AQUI
    usb_max_size = usb_endpoint_maxp(usb_endpoint_in);
    usb_in = usb_endpoint_in->bEndpointAddress;
    usb_out = usb_endpoint_out->bEndpointAddress;
    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    // Verificar se aqui mudar mesmo
    // LDR_value = usb_read_serial();
    LDR_value = usb_send_cmd("GET_LDR", -1);

    printk("LDR Value: %d\n", LDR_value);

    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    if (sys_obj) kobject_put(sys_obj);      // Remove os arquivos em /sys/kernel/smartlamp
    kfree(usb_in_buffer);                   // Desaloca buffers
    kfree(usb_out_buffer);
}

// Envia um comando via USB, espera e retorna a resposta do dispositivo (convertido para int)
// Exemplo de Comando:  SET_LED 80
// Exemplo de Resposta: RES SET_LED 1
// Exemplo de chamada da função usb_send_cmd para SET_LED: usb_send_cmd("SET_LED", 80);
static int usb_send_cmd(char *cmd, int param) {
    int ret, actual_size;
    int retries = 30;
    char resp_expected[MAX_RECV_LINE];
    int value;

    printk(KERN_INFO "SmartLamp: Enviando comando: %s\n", cmd);

    if (param == -1) {
        snprintf(usb_out_buffer, usb_max_size, "%s\n", cmd);
    } else {
        snprintf(usb_out_buffer, usb_max_size, "%s %d\n", cmd, param);
    }

    ret = usb_bulk_msg(smartlamp_device,
                       usb_sndbulkpipe(smartlamp_device, usb_out),
                       usb_out_buffer,
                       min(usb_max_size, MAX_RECV_LINE),
                       &actual_size,
                       1000);
    if (ret) {
        printk(KERN_ERR "SmartLamp: Erro ao enviar comando, codigo %d!\n", ret);
        return -1;
    }

    printk(KERN_INFO "SmartLamp: Comando enviado com sucesso!\n");

    // Prefixo esperado com espaço para facilitar o parsing
    sprintf(resp_expected, "RES %s ", cmd);

    while (retries > 0) {
        ret = usb_bulk_msg(smartlamp_device,
                           usb_rcvbulkpipe(smartlamp_device, usb_in),
                           usb_in_buffer,
                           min(usb_max_size, MAX_RECV_LINE) - 1,
                           &actual_size,
                           1000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d), codigo %d\n", retries, ret);
            retries--;
            ssleep(2);
            continue;
        }

        if (actual_size > 0) {
            usb_in_buffer[actual_size] = '\0';  // termina string
            printk(KERN_INFO "SmartLamp: Dados recebidos:\n%s\n", usb_in_buffer);

            // Busca o prefixo na string recebida
            char *pos = strstr(usb_in_buffer, resp_expected);
            if (pos) {
                // Pula o prefixo para o começo do número
                pos += strlen(resp_expected);
                // Tenta ler o número
                if (sscanf(pos, "%d", &value) == 1) {
                    printk(KERN_INFO "SmartLamp: Valor extraído: %d\n", value);
                    return value;
                } else {
                    printk(KERN_ERR "SmartLamp: Prefixo encontrado, mas falha ao extrair valor.\n");
                }
            } else {
                printk(KERN_ERR "SmartLamp: Prefixo '%s' não encontrado na mensagem.\n", resp_expected);
            }
        }

        retries--;
        ssleep(2);
    }

    printk(KERN_ERR "SmartLamp: Falha ao obter resposta válida após múltiplas tentativas.\n");
    return -1;
}


// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff) {
    // value representa o valor do led ou ldr
    int value;
    // attr_name representa o nome do arquivo que está sendo lido (ldr ou led)
    const char *attr_name = attr->attr.name;

    // printk indicando qual arquivo está sendo lido
    printk(KERN_INFO "SmartLamp: Lendo %s ...\n", attr_name);

    // Implemente a leitura do valor do led ou ldr usando a função usb_send_cmd()
    if (strcmp(attr_name, "ldr") == 0)
        value = usb_send_cmd("GET_LDR", -1);
    else if (strcmp(attr_name, "led") == 0)
        value = usb_send_cmd("GET_LED", -1);
    else if (strcmp(attr_name, "temp") == 0)
        value = usb_send_cmd("GET_TEMP", -1);
    else if (strcmp(attr_name, "hum") == 0)
        value = usb_send_cmd("GET_HUM", -1);
    else
        return -EINVAL;

    sprintf(buff, "%d\n", value);                 // Cria a mensagem com o valor do led, ldr
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

    // utilize a função usb_send_cmd para enviar o comando SET_LED X
    if (strcmp(attr_name, "led") == 0)
        ret = usb_send_cmd("SET_LED", value);

    if (ret < 0) {
        printk(KERN_ALERT "SmartLamp: erro ao setar o valor do %s.\n", attr_name);
        return -EACCES;
    }

    return strlen(buff);
}