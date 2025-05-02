#include<stdio.h>
#include<string.h>
#include<fcntl.h>
#include<unistd.h>
struct wav_header{
    char riff[4];        // "RIFF"
    int size;           // size of the file in bytes
    char wave[4];       // "WAVE"
    char fmt[4];        // "fmt "
    int fmt_size;      // size of the fmt chunk
    short format_type; // format type (1 for PCM)
    short channels;     // number of channels
    int sample_rate;   // sample rate
    int byte_rate;     // byte rate
    short block_align; // block align
    short bits_per_sample; // bits per sample
    char data[4];      // "data"
    int data_size;     // size of the data chunk
};

int is_little_endian() {
    unsigned int x = 1; // 0x00000001
    return *((char*)&x); // 如果最低字节是1则是小端

    //大端: [0x00] [0x00] [0x00] [0x01]
    //小端: [0x01] [0x00] [0x00] [0x00]
}

void print(struct wav_header *header) {
    printf("wav文件头结构体大小: %d\n", (int)sizeof(struct wav_header));
    printf("RIFF标识: %.4s\n", header->riff);
    printf("文件大小: %d\n", header->size);
    printf("文件格式: %.4s\n", header->wave);
    printf("格式块标识: %.4s\n", header->fmt);
    printf("格式块长度: %d\n", header->fmt_size);
    printf("编码格式代码: %d\n", header->format_type);
    printf("声道数: %d\n", header->channels);
    printf("采样频率: %d\n", header->sample_rate);
    printf("传输速率: %d\n", header->byte_rate);
    printf("数据块对齐单位: %d\n", header->block_align);
    printf("采样位数(长度): %d\n", header->bits_per_sample);
    printf("数据块标识: %.4s\n", header->data);
    printf("数据块长度: %d\n", header->data_size);
}

void fprint(struct wav_header *header, int out_fd) {

    char buff[256];
    sprintf(buff, "wav文件头结构体大小: %d\n", (int)sizeof(struct wav_header));
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "RIFF标识: %.4s\n", header->riff);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "文件大小: %d\n", header->size);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "文件格式: %.4s\n", header->wave);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "格式块标识: %.4s\n", header->fmt);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "格式块长度: %d\n", header->fmt_size);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "编码格式代码: %d\n", header->format_type);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "声道数: %d\n", header->channels);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "采样频率: %d\n", header->sample_rate);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "传输速率: %d\n", header->byte_rate);
    write(out_fd, buff, strlen(buff));
    
    sprintf(buff, "数据块对齐单位: %d\n", header->block_align);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "采样位数(长度): %d\n", header->bits_per_sample);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "数据块标识: %.4s\n", header->data);
    write(out_fd, buff, strlen(buff));

    sprintf(buff, "数据块长度: %d\n", header->data_size);
    write(out_fd, buff, strlen(buff));
}

void short_little_to_big(char* data) {
    char temp = data[0];
    data[0] = data[1];
    data[1] = temp;
}

void int_little_to_big(char* data) {
    char temp = data[0];
    data[0] = data[3];
    data[3] = temp;

    temp = data[1];
    data[1] = data[2];
    data[2] = temp;
}

int main(int argn, char *argv[]){

    char filename[256];
    char output_filename[256];
    if(argn != 2){
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }
    strcpy(filename, argv[1]);


    if (strcmp(filename + strlen(filename) - 4, ".wav") != 0) {
        printf("Error: The file must be a .wav file\n");
        return 1;
    }

    strncpy(output_filename, filename, strlen(filename) - 4);
    output_filename[strlen(filename) - 4] = '\0';
    strcat(output_filename, ".txt");

    int fd = open(filename, O_RDONLY);
    if(fd < 0){
        printf("Error opening file: %s\n", filename);
        return 1;
    }
    lseek(fd, 0, SEEK_SET);


    int out_fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(out_fd < 0){
        printf("Error creating file: %s\n", output_filename);
        return 1;
    }

    struct wav_header header;
    // Read the WAV header
    ssize_t bytesRead = read(fd, &header, sizeof(header));
    if (bytesRead != sizeof(header)) {
        printf("Error reading WAV header from file: %s\n", filename);
        close(fd);
        return 1;
    }

    printf("is little endian: %s\n", is_little_endian() ? "true" : "false");

    if (!is_little_endian()) {
        //wav字符串格式的是大端, 数字格式是小端
        //字符串的大端是通用的形式
        //如果系统不是小端, 需要将读取的数据转换为大端读取
        
        int_little_to_big((char*)&header.size); // size
        int_little_to_big((char*)&header.fmt_size); // fmt_size
        short_little_to_big((char*)&header.format_type); // format_type
        short_little_to_big((char*)&header.channels); // channels
        int_little_to_big((char*)&header.sample_rate); // sample_rate
        int_little_to_big((char*)&header.byte_rate); // byte_rate
        short_little_to_big((char*)&header.block_align); // block_align
        short_little_to_big((char*)&header.bits_per_sample); // bits_per_sample
        int_little_to_big((char*)&header.data_size); // data_size
    }



    print(&header);
    fprint(&header, out_fd);
    close(fd);
}