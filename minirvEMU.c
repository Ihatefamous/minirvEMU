#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <am.h>
#include <klib-macros.h>

#define M_SIZE 0x1000000
uint32_t PC = 0;
uint32_t R[31];
uint32_t M[M_SIZE];
uint32_t pixels[256][256] = {0} ;

typedef enum {
    I_type, R_type, S_type, L_type, LUI, JALR
} Operation ;

char Op_str [][10] = {"I_type", "R_type", "S_type", "L_type", "LUI", "JALR"} ;

Operation Ins_type = I_type ;
uint8_t funct3 = 0 ;
uint32_t rs1_addr, rs2_addr, rd_addr, rs1_value, rs2_value, alu_result, imm_value, memory_value  ;

int read_reg(uint32_t addr, uint32_t* value){
    if(addr==0) {
        *value = 0 ;
    } else if (addr>31) {
        return -1 ;
    } else {
        *value = R[addr-1] ;
    }
    return 0 ;
}

int write_reg(uint32_t addr, uint32_t value){
    if (addr==0||addr>31){
        return -1;
    } else {
        R[addr-1] = value ;
        return 0;
    }
}

int write_back(uint32_t addr, uint32_t alu_result, uint32_t pc_4, uint32_t imm, uint32_t memory_value){
    switch (Ins_type)
    {
    case I_type:
    case R_type:
        return write_reg(addr, alu_result) ;
    case L_type:
        return write_reg(addr, memory_value) ;
    case LUI:
        return write_reg(addr, imm) ;
    case JALR:
        return write_reg(addr, pc_4) ;
    case S_type: return 0 ;
    default:
        return -1;
    }
}

int imm_decode (uint32_t ins, uint32_t *value) {

    switch (Ins_type)
    {
    case I_type:
    case L_type:
    case JALR:
        *value = (uint32_t)((int32_t)ins>>20) ;
        break;

    case S_type:
        *value = (uint32_t)((ins>>7) & 0b11111) ;
        *value = *value | (uint32_t)((int32_t)(ins&0xFE000000)>>20) ;
        break;

    case LUI:
        *value = ins & 0xFFFFF000 ;
        break;

    default:
        *value = 0 ;
        return -1 ;
    }

    return 0 ;
}

int type_decode (uint32_t ins) {
    rs1_addr = (ins >> 15) & 0b11111 ;
    rs2_addr = (ins >> 20) & 0b11111 ;
    rd_addr = (ins >> 7) & 0b11111 ;
    switch ((ins & 0x7F) >> 2 )
    {
    case 0b01101:
        Ins_type = LUI ;
        funct3 = 0 ;
        break;

    case 0b11001:
        Ins_type = JALR ;
        funct3 = 0 ;
        break;

    case 0b00000:
        Ins_type = L_type ;
        funct3 = (uint8_t)((ins >> 12) & 0b111) ;
        break;

    case 0b01000:
        Ins_type = S_type ;
        funct3 = (uint8_t)((ins >> 12) & 0b111);
        break;

    case 0b00100:
        Ins_type = I_type ;
        funct3 = (uint8_t)((ins >> 12) & 0b111) ;
        break;

    case 0b01100:
        Ins_type = R_type ;
        funct3 = (uint8_t)((ins >> 12) & 0b111) ;
        break;

    default:
        Ins_type = I_type ;
        funct3 = 0 ;
        printf("decode error\n");
        return -1 ;
    }
    return 0;
}

int save_memory(uint32_t addr, uint32_t value) {
    if (Ins_type != S_type){
        return 0 ;
    }
    if (funct3 == 0) {
        uint32_t result ;
        addr &= M_SIZE-1 ;
        result = M[addr>>2] ;
        value &= 0xFF ;
        result &= ~(0xFF<<((addr&0b11)*8)) ;
        result |= (value << ((addr&0b11)*8)) ;
        M[addr>>2] = result ;
    } else if (funct3 == 2) {
        if ((addr >= 0x20000000) && (addr < 0x20040000)) {
            pixels[(addr>>10)&0xFF][(addr>>2)&0xFF] = value ;
            io_write(AM_GPU_FBDRAW, 0, 0, pixels, 256, 256, true) ;
        } else {
            addr &= M_SIZE-1 ;
            M[addr>>2] = value ;
        }
    } else {
        printf("save error\n") ;
        return -2 ;
    }

    return 1 ;
}

int load_memory (uint32_t addr, uint32_t *value) {
    if (Ins_type != L_type){
        return 0 ;
    }
    if (addr>>2 >= M_SIZE) {
        return -1;
    }
    addr &= M_SIZE-1 ;
    *value = M[addr>>2] ;
    if (funct3 == 4){
        *value = (*value>>((addr&0b11)*8))&0xFF ;
    } else if (funct3!=2){
        printf("load error\n") ;
        return -2 ;
    }
    return 1 ;
}

int update_PC (uint32_t value) {
    switch (Ins_type)
    {
    case LUI:
    case L_type :
    case S_type :
    case I_type :
    case R_type :
        PC += 4 ;
        break;
    case JALR :
        PC = value ;
        break;
    default:
        return -1 ;
    }
    return 0 ;
}

uint32_t ALU (uint32_t rs1, uint32_t rs2, uint32_t imm){
    uint32_t result = 0 ;
    switch (Ins_type)
    {
    case JALR:
    case L_type:
    case S_type:
    case I_type:
        result = rs1 + imm ;
        break;
    case R_type:
        result = rs1 + rs2 ;
    default:
        break;
    }
    return result ;
}

int cycle(){
    uint32_t ins = M[PC>>2] ;
    type_decode(ins) ;
    imm_decode(ins, &imm_value) ;
    read_reg(rs1_addr, &rs1_value) ;
    read_reg(rs2_addr, &rs2_value) ;
    alu_result = ALU(rs1_value, rs2_value, imm_value) ;
    load_memory(alu_result, &memory_value) ;
    save_memory(alu_result, rs2_value) ;
    write_back(rd_addr, alu_result, PC+4, imm_value, memory_value) ;
    update_PC(alu_result) ;
    return 0;
}


int main(){

    FILE *file;
    size_t bytes_read;
    size_t elements_to_read;
    ioe_init();
    file = fopen("./logisim-bin/vga.bin", "rb");
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    elements_to_read = file_size / sizeof(uint32_t);
    bytes_read = fread(M, sizeof(uint32_t), elements_to_read, file);
    if (bytes_read != elements_to_read) {
        fclose(file);
        return 1;
    }
    fclose(file);

    while(1)
    {
        if (cycle()!=0){
            return -1 ;
        }
    }
    getchar();
    return 0;

}