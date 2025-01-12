#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include "hash.h"


#define MAX_LINE_LENGTH 66
#define MAX_LABEL_LENGTH 8
#define MAX_OPCODE_LENGTH 6
#define MAX_OPERAND_LENGTH 18
#define MAX_COMMENT_LENGTH 31
#define OPTAB_SIZE 101
#define SYMTAB_SIZE 211
#define EXPR_ERROR -1
#define EXPR_OVERFLOW -2

FILE *debugFile; // 全域變數，用於除錯輸出
// // fprintf(debugFile, "This is a debug message\n");


typedef enum LineTypes
{
    EMPTY,
    COMMENT,
    INSTRUCTION
} LineType;


typedef enum // lecture note: Assembler p.63
{
    ADDR_MODE_RELATIVE,
    ADDR_MODE_INDIRECT,
    ADDR_MODE_IMMEDIATE,
    ADDR_MODE_INDEX
} AddressingMode;

typedef struct
{
    LineType type;
    union
    {
        struct // lecture note: Assembler p.15
        {
            char label[MAX_LABEL_LENGTH + 1];
            char operation[MAX_OPCODE_LENGTH + 1];
            char operand[MAX_OPERAND_LENGTH + 1];
            char comment[MAX_COMMENT_LENGTH + 1];
            AddressingMode addrmode;
            bool isExtend;
            int LOCATION;
        } Instruction;

        char lineComment[MAX_LINE_LENGTH + 1];
    } content;
} LineData;

/*directive*/

typedef struct
{
    char name[MAX_LABEL_LENGTH + 1];
    int address;
    bool isRef;  // true for EXTREF, false for EXTDEF
} ExternalSymbol;

typedef struct
{
    char name[MAX_LABEL_LENGTH + 1];
    int startAddress;
    int length;
    ExternalSymbol *externals;
    int externalCount;
} ControlSection;
/*directive*/
typedef struct
{
    int startingAddress;
    int programLength;
    int lineNumber;
    char progamName[7];
    bool standardSIC;
    LineData **lines;
    /*directive*/
    ControlSection *sections;
    int sectionCount;
    int currentSection;    
    int baseRegisterValue;  // 新增：用於 Base-relative 尋址
} IntermediateData;


typedef enum {
    DIR_ORG,
    DIR_EQU,
    DIR_EXTREF,
    DIR_EXTDEF,
    DIR_CSECT
} DirectiveType;

typedef struct {
    int (*handler)(LineData*, IntermediateData*, HashTable*);
    DirectiveType type;
} DirectiveValue;

typedef struct
{
    int opcode;
    int format;
    bool isXE;
} OPTABValue;

typedef struct
{
    int address;
    int flag;
} SYMTABValue;

IntermediateData *pass1(FILE *file, HashTable *OPTAB, HashTable *SYMTAB);
void pass2(IntermediateData *interData, FILE *lstfile, FILE *objfile, HashTable *OPTAB, HashTable *SYMTAB);
LineData *readLine(FILE *file, int lineNumber);
OPTABValue *createOPTABValue(int opcode, int format, bool isXE);
SYMTABValue *createSYMTABValue(int address, int flag);
void setOPTAB(HashTable *OPTAB);
void copy_and_trim(char *dest, const char *src, size_t len);
bool isStandardSICCode(IntermediateData *interdata, HashTable *OPTAB);
/*
 * concatenate string
 * argument:
 * numPairs: the number of string to be concatenated
 * ...: the string, location pair.
 * usage: if you want to concatenate three string "A", "B", "C" together,
 * you can use this function like concatenateStr(3,"A",0,"B",1,"C",2)
 * and use a char* variable to receive it. The output string is "ABC"
 */
char *concatenateStr(int numPairs, ...);
void freeOPTABValue(OPTABValue *v);
void freeSYMTABValue(SYMTABValue *v);

// Directive related functions
void setDirectiveTable(HashTable *DIRTAB);
DirectiveValue *createDirectiveValue(int (*handler)(LineData*, IntermediateData*, HashTable*), DirectiveType type) {
    DirectiveValue *directive = (DirectiveValue *)malloc(sizeof(DirectiveValue));
    if (!directive) {
        fprintf(stderr, "Memory allocation failed for directive value\n");
        return NULL;
    }
    
    directive->handler = handler;
    directive->type = type;
    
    return directive;
}
int handleORG(LineData *line, IntermediateData *data, HashTable *SYMTAB);
int handleEQU(LineData *line, IntermediateData *data, HashTable *SYMTAB);
int handleEXTREF(LineData *line, IntermediateData *data, HashTable *SYMTAB);
int handleEXTDEF(LineData *line, IntermediateData *data, HashTable *SYMTAB);
int handleCSECT(LineData *line, IntermediateData *data, HashTable *SYMTAB);

// 新增輔助函數的宣告
int evaluateExpression(const char *expression, HashTable *SYMTAB, int *error);
int evaluateTerm(const char **expression, HashTable *SYMTAB, int *error);
int evaluateFactor(const char **expression, HashTable *SYMTAB, int *error);
bool checkAddressOverflow(int value);

int getRegisterNum(char reg);

int main(/*int argc, char *argv[]*/)
{
    // 開啟 debug 檔案
    debugFile = fopen("debug.log", "w");
    if (!debugFile) {
        perror("Failed to open debug.log");
        return -1;
    }

    const char *filename = "SRCFILE";
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Error opening file");
        return -1;
    }
    HashTable *OPTAB = createHashTable(OPTAB_SIZE);
    setOPTAB(OPTAB);

    HashTable *SYMTAB = createHashTable(SYMTAB_SIZE);
    IntermediateData *interdata = pass1(file, OPTAB, SYMTAB);
    fclose(file);

    FILE *listFile = fopen("lstfile", "w");
    FILE *objFile = fopen("objfile", "w");
    pass2(interdata,listFile,objFile,OPTAB,SYMTAB);

    fclose(debugFile);

    return 0;
}

IntermediateData *pass1(FILE *file, HashTable *OPTAB, HashTable *SYMTAB)
{
    int lineNumber = 0;
    int LOCCTR = 0;
    IntermediateData *interData = (IntermediateData *)calloc(1, sizeof(IntermediateData));
    interData->lines = (LineData **)calloc(100, sizeof(LineData *));
    /*directive*/
    interData->sections = (ControlSection *)calloc(10, sizeof(ControlSection)); // 假設最多10個控制段
    interData->sectionCount = 0;
    interData->currentSection = -1;
    LineData *currentLine = readLine(file, lineNumber);
    interData->lines[lineNumber++] = currentLine;
    if (currentLine->type == INSTRUCTION && !strcmp(currentLine->content.Instruction.operation, "START"))
    {
        char *endptr;
        int startingAddress = (int)strtol(currentLine->content.Instruction.operand, &endptr, 16);
        if (startingAddress >= 32768)
        {
            fprintf(stderr, "The addressing space is 0 to 32767.\n");
        }

        if (*endptr == '\0')
        {
            interData->startingAddress = startingAddress;
            LOCCTR = startingAddress;
            currentLine->content.Instruction.LOCATION = startingAddress;
            strcpy(interData->progamName, currentLine->content.Instruction.label);
            currentLine = readLine(file, lineNumber);
            interData->lines[lineNumber++] = currentLine;
        }
    }
    else
    {
        interData->startingAddress = 0;
        LOCCTR = 0;
    }

    while (!feof(file) && strcmp(currentLine->content.Instruction.operation, "END"))
    {
        if (currentLine->type == INSTRUCTION)
        {
            currentLine->content.Instruction.LOCATION = LOCCTR;
            
            // 處理標籤（如果有的話）
            if (strlen(currentLine->content.Instruction.label) > 0)
            {
                void *foundValue;
                if (searchHashTable(SYMTAB, currentLine->content.Instruction.label, &foundValue))
                {
                    SYMTABValue *value = (SYMTABValue *)foundValue;
                    value->flag = 1; // duplicate symbol
                    fprintf(stderr, "Error, duplicate symbol at line %d\n", lineNumber);
                }
                else
                {
                    // 將符號加入符號表
                    insertHashTable(SYMTAB, currentLine->content.Instruction.label, createSYMTABValue(LOCCTR, 0));
                }
            }

            // 處理指令或偽指令
            void *foundOpValue;
            // if (currentLine->content.Instruction.operation[0] == '+') {
            //     // fprintf(debugFile, "有");
            // }
            char operation_noplus[MAX_OPCODE_LENGTH + 1];
            if (currentLine->content.Instruction.operation[0] == '+') {
                // 如果第一個字是 '+'，將運算碼的內容（去掉 '+'）複製到新變數中
                strcpy(operation_noplus, currentLine->content.Instruction.operation + 1);
            } else {
                // 否則直接複製整個運算碼
                strcpy(operation_noplus, currentLine->content.Instruction.operation);
            }

            if (searchHashTable(OPTAB, operation_noplus, &foundOpValue))
            {
                OPTABValue *opValue = (OPTABValue *)foundOpValue;

                switch (opValue->format)
                {
                case 1: // type 1
                    LOCCTR += 1;
                    break;
                case 2: // type 2
                    LOCCTR += 2;
                    break;
                case 3: // type 3/4
                    if (currentLine->content.Instruction.isExtend)
                    {
                        // fprintf(debugFile, "有加4喔\n");
                        LOCCTR += 4;
                    }
                    else
                    {
                        LOCCTR += 3;
                    }
                    break;
                default:
                    break;
                }
            }
            else if (!strcmp(currentLine->content.Instruction.operation, "WORD"))
            {
                LOCCTR += 3;
                // 如果 WORD 有標籤，需要加入符號表
                if (strlen(currentLine->content.Instruction.label) > 0)
                {
                    insertHashTable(SYMTAB, currentLine->content.Instruction.label, createSYMTABValue(LOCCTR - 3, 0));
                }
            }
            else if (!strcmp(currentLine->content.Instruction.operation, "RESW"))
            {
                int num = (int)strtol(currentLine->content.Instruction.operand, NULL, 10);
                LOCCTR += (num * 3);
                // 如果 RESW 有標籤，需要加入符號表
                if (strlen(currentLine->content.Instruction.label) > 0)
                {
                    insertHashTable(SYMTAB, currentLine->content.Instruction.label, createSYMTABValue(LOCCTR - (num * 3), 0));
                }
            }
            else if (!strcmp(currentLine->content.Instruction.operation, "BYTE"))
            {
                int length = 0;
                if (currentLine->content.Instruction.operand[0] == 'C')
                {
                    const char *start = strchr(currentLine->content.Instruction.operand, '\'');
                    const char *end = strrchr(currentLine->content.Instruction.operand, '\'');
                    if (start && end && start != end)
                    {
                        length = end - start - 1;
                    }
                }
                else if (currentLine->content.Instruction.operand[0] == 'X')
                {
                    const char *start = strchr(currentLine->content.Instruction.operand, '\'');
                    const char *end = strrchr(currentLine->content.Instruction.operand, '\'');
                    if (start && end && start != end)
                    {
                        length = (end - start - 1) / 2;
                    }
                }
                LOCCTR += length;
                // 如果 BYTE 有標籤，需要加入符號表
                if (strlen(currentLine->content.Instruction.label) > 0)
                {
                    insertHashTable(SYMTAB, currentLine->content.Instruction.label, createSYMTABValue(LOCCTR - length, 0));
                }
            }
            else if (!strcmp(currentLine->content.Instruction.operation, "RESB"))
            {
                int num = (int)strtol(currentLine->content.Instruction.operand, NULL, 10);
                LOCCTR += num;
                // 如果 RESB 有標籤，需要加入符號表
                if (strlen(currentLine->content.Instruction.label) > 0)
                {
                    insertHashTable(SYMTAB, currentLine->content.Instruction.label, createSYMTABValue(LOCCTR - num, 0));
                }
            }
            else if (!strcmp(currentLine->content.Instruction.operation, "EQU"))
            {
                // 處理 EQU 指令
                if (strlen(currentLine->content.Instruction.label) > 0)
                {
                    int value;
                    if (strcmp(currentLine->content.Instruction.operand, "*") == 0)
                    {
                        value = LOCCTR;
                    }
                    else
                    {
                        // 處理表達式
                        int error = 0;
                        value = evaluateExpression(currentLine->content.Instruction.operand, SYMTAB, &error);
                        if (error)
                        {
                            fprintf(stderr, "Error evaluating EQU expression at line %d\n", lineNumber);
                            continue;
                        }
                    }
                    insertHashTable(SYMTAB, currentLine->content.Instruction.label, createSYMTABValue(value, 0));
                }
            }
        }
        currentLine = readLine(file, lineNumber);
        interData->lines[lineNumber++] = currentLine;
        if (lineNumber == 100)
        {
            interData->lines = (LineData **)realloc(interData->lines, 200 * sizeof(LineData *));
            if (!interData->lines)
            {
                fprintf(stderr, "Fail to realloc memory\n");
            }
        }
    }
    interData->lines[lineNumber] = currentLine;
    interData->programLength = LOCCTR - interData->startingAddress;
    currentLine->content.Instruction.LOCATION = LOCCTR;
    interData->lines = (LineData **)realloc(interData->lines, lineNumber * sizeof(LineData *));
    interData->lineNumber = lineNumber;
    interData->standardSIC = isStandardSICCode(interData, OPTAB) ? true : false;
    return interData;
}

void pass2(IntermediateData *interData, FILE *lstfile, FILE *objfile, HashTable *OPTAB, HashTable *SYMTAB) {
    void *foundOpValue;
    int lineNumber = 0;
    LineData *currentLine = interData->lines[lineNumber++];
    int temp=0;

    // 處理列表文件的START指令輸出
    char *printStr = NULL;
    if (currentLine->type == INSTRUCTION && !strcmp(currentLine->content.Instruction.operation, "START")) {
        printStr = concatenateStr(5, currentLine->content.Instruction.operand, 0,
                                  currentLine->content.Instruction.label, 12,
                                  currentLine->content.Instruction.operation, 21,
                                  currentLine->content.Instruction.operand, 29,
                                  currentLine->content.Instruction.comment, 47);
        fprintf(lstfile, "%s", printStr);
        free(printStr);
        fprintf(lstfile, "\n");
        currentLine = interData->lines[lineNumber++];
    }

    // 輸出目標文件的H記錄
    char startingAddr[7], programLength[7];
    snprintf(startingAddr, 7, "%06X", interData->startingAddress);
    snprintf(programLength, 7, "%06X", interData->programLength);
    printStr = concatenateStr(4, "H", 0,
                              interData->progamName, 1,
                              startingAddr, 7,
                              programLength, 13);
    fprintf(objfile, "%s\n", printStr);
    free(printStr);

    // T record 處理
    unsigned int objCode;
    char TRecord[70] = {0};
    int TRecordCounter = 0;
    int TRecordStart = currentLine->content.Instruction.LOCATION;

    while (lineNumber <= interData->lineNumber) {
        if (currentLine->type == INSTRUCTION) {
            // 列表文件輸出
            char locationStr[7];
            char objCodeStr[9] = "      "; // 預設為6個空格
            snprintf(locationStr, 7, "%04X", currentLine->content.Instruction.LOCATION);

            // 生成目標碼字串
            char operation_noplus[MAX_OPCODE_LENGTH + 1];
            if (currentLine->content.Instruction.operation[0] == '+') {
                // 如果第一個字是 '+'，將運算碼的內容（去掉 '+'）複製到新變數中
                // fprintf(debugFile,"正確刪掉+");
                strcpy(operation_noplus, currentLine->content.Instruction.operation + 1);
            } else {
                // 否則直接複製整個運算碼
                strcpy(operation_noplus, currentLine->content.Instruction.operation);
            }

            if (searchHashTable(OPTAB, operation_noplus, &foundOpValue)) {
                snprintf(objCodeStr, 9, "%06X", objCode);
            }
            else if (!strcmp(currentLine->content.Instruction.operation, "BYTE")) {
                // 處理 BYTE 指令的目標碼顯示
                if (currentLine->content.Instruction.operand[0] == 'C') {
                    char *start = strchr(currentLine->content.Instruction.operand, '\'') + 1;
                    char *end = strrchr(currentLine->content.Instruction.operand, '\'');
                    if (start && end && start != end) {
                        int i;
                        char temp[3];
                        objCodeStr[0] = '\0';
                        for (i = 0; i < end - start && i < 3; i++) {
                            snprintf(temp, 3, "%02X", (unsigned char)start[i]);
                            strcat(objCodeStr, temp);
                        }
                    }
                }
                else if (currentLine->content.Instruction.operand[0] == 'X') {
                    char *start = strchr(currentLine->content.Instruction.operand, '\'') + 1;
                    char *end = strrchr(currentLine->content.Instruction.operand, '\'');
                    if (start && end && start != end) {
                        strncpy(objCodeStr, start, end - start);
                        objCodeStr[end - start] = '\0';
                    }
                }
            }
            else if (!strcmp(currentLine->content.Instruction.operation, "WORD")) {
                int value = 0;
                int error = 0;
                value = evaluateExpression(currentLine->content.Instruction.operand, SYMTAB, &error);
                if (!error) {
                    snprintf(objCodeStr, 7, "%06X", value & 0xFFFFFF);
                }
            }

            // 修改列表檔輸出格式，加入目標碼
            printStr = concatenateStr(6, locationStr, 0,
                                      objCodeStr, 6,
                                      currentLine->content.Instruction.label, 12,
                                      currentLine->content.Instruction.operation, 21,
                                      currentLine->content.Instruction.operand, 29,
                                      currentLine->content.Instruction.comment, 47);
            fprintf(lstfile, "%s", printStr);
            free(printStr);
            fprintf(lstfile, "\n");

            void *foundOpValue, *foundSymValue;
            objCode = 0;  // 重置 objCode

            // 處理指令
            // // fprintf(debugFile,"operation_noplus=%s",operation_noplus);
            if (searchHashTable(OPTAB, operation_noplus, &foundOpValue)) {
                OPTABValue *opValue = (OPTABValue *)foundOpValue;
                uint8_t baseOpcode = (opValue->opcode & 0xFC); // 高6位 opcode
                objCode = (uint32_t)baseOpcode << 16;

                // 檢查是否有操作數
                if (currentLine->content.Instruction.operand[0] != '\0') {
                    char operandCopy[MAX_OPERAND_LENGTH + 1];
                    strcpy(operandCopy, currentLine->content.Instruction.operand);
                    char *symbolPart = operandCopy;
                    int operandAddress = 0;

                    // 設置 n 和 i 位
                    switch (currentLine->content.Instruction.addrmode) {
                        case ADDR_MODE_IMMEDIATE:
                            if (isdigit(currentLine->content.Instruction.operand[1])) {
                                int immediateValue = atoi(&currentLine->content.Instruction.operand[1]);//用來判別format
                                if (immediateValue <= 4095) {
                                    // 小於等於 4095 的處理
                                    // // fprintf(debugFile, "This is a debug message\n");
                                    objCode |= 0x010000;  // 設置 i 位
                                    objCode |= immediateValue & 0xFFF;  // 低 12 位直接寫入目標碼
                                }
                                else { //大於4096
                                    // fprintf(debugFile, "immediateValue=%d\n",immediateValue);
                                    objCode |= 0x01000000;  // 設置 i 位
                                    objCode |= 0x00100000;  // 設置 e 位 (e=1, 表示擴展格式)
                                    objCode |= immediateValue & 0xFFFFF;  // 取低 20 位
                                    // fprintf(debugFile, "obj=%08x\n", objCode);
                                }
                                // objCode |= atoi(&currentLine->content.Instruction.operand[1]) & 0xFFF;
                            } else if (searchHashTable(SYMTAB, &currentLine->content.Instruction.operand[1], &foundSymValue)) {
                                fprintf(debugFile, "進來了\n");
                                objCode |= 0x010000;
                                SYMTABValue *symValue = (SYMTABValue *)foundSymValue;
                                int pc = currentLine->content.Instruction.LOCATION + 3; // 計算目前指令的 PC 值 (下一指令的地址)
                                int disp = symValue->address - pc; // 計算 PC 相對偏移量
                                temp=disp;
                                // 檢查偏移量是否在範圍內 (-2048 ~ 2047)
                                if (disp >= -2048 && disp <= 2047) {
                                    objCode |= 0x002000;           // 設定 p 位 (PC-relative addressing)
                                    objCode |= (disp & 0xFFF);    // 設定偏移量的低 12 位
                                    fprintf(debugFile, "PC 定址成功，disp= %d, objCode= %06x\n", disp, objCode);
                                } else {
                                    fprintf(debugFile, "不行PC 定址\n)") ;
                                }
                                fprintf(debugFile, "這裡，symValue= %x\n",symValue->address);
                                fprintf(debugFile, "objCode= %06x\n",objCode);
                            } else {
                                fprintf(stderr, "Error: Undefined symbol '%s'\n", currentLine->content.Instruction.operand);
                            }                                
                            break;
                        case ADDR_MODE_INDIRECT:
                            objCode |= 0x020000; // n=1, i=0
                            symbolPart = operandCopy + 1; // 跳過 @ 符號
                            goto search_symbol;
                        case ADDR_MODE_RELATIVE:
                        default:
                            objCode |= 0x030000; // n=1, i=1
                            goto search_symbol;
                    }

                    // 處理符號查找
                    search_symbol:
                    if (operandAddress == 0) { // 如果不是立即數
                        // 處理索引定址
                        char *comma = strchr(symbolPart, ',');
                        if (comma) {
                            *comma = '\0'; // 分割符號和索引部分
                            if (strcmp(comma + 1, "X") == 0) {
                                objCode |= 0x8000; // 設置 x 位
                            } else {
                                fprintf(stderr, "Error: Invalid index register at line %d\n", lineNumber);
                            }
                        }

                        // 在SYMTAB中查找符號
                        if (searchHashTable(SYMTAB, symbolPart, &foundSymValue)) {
                            SYMTABValue *symValue = (SYMTABValue *)foundSymValue;
                            operandAddress = symValue->address;
                        } else {
                            fprintf(stderr, "Error: Undefined symbol '%s' at line %d\n", symbolPart, lineNumber);
                            operandAddress = 0;
                        }
                    }

                    // 生成目標碼
                    if (currentLine->content.Instruction.isExtend) {
                        objCode |= 0x100000; // e=1
                        objCode |= (operandAddress & 0xFFFFF); // 20-bit 地址
                        // fprintf(debugFile, "obj=%08x\n", objCode);
                    } else {
                        int pc = currentLine->content.Instruction.LOCATION + 3; // 下一條指令地址
                        int disp = operandAddress - pc;
                        if (currentLine->content.Instruction.operand[1] == 'T' && currentLine->content.Instruction.operand[0] == '#'){
                            disp=temp;
                        }

                        // 嘗試PC相對尋址
                        if (disp >= -2048 && disp <= 2047) {
                            objCode |= 0x2000; // p=1
                            objCode |= (disp & 0xFFF);
                        } 
                        // 嘗試基址相尋址
                        else if (interData->baseRegisterValue >= 0) {
                            disp = operandAddress - interData->baseRegisterValue;
                            if (disp >= 0 && disp <= 4095) {
                                objCode |= 0x4000; // b=1
                                objCode |= (disp & 0xFFF);
                                if (currentLine->content.Instruction.operand[1] = 'T'){
                                    fprintf(debugFile,"不小心進來了，objCode=%06x\n",objCode);
                                }
                            } else {
                                fprintf(stderr, "Error: Address out of range for base-relative addressing at line %d\n", lineNumber);
                            }
                        } else {
                            fprintf(stderr, "Error: Address out of range and no BASE register set at line %d\n", lineNumber);
                        }
                    }
                }

                // 將指令目標碼加入T記錄
                // snprintf(TRecord + TRecordCounter, 
                //         currentLine->content.Instruction.isExtend ? 9 : 7, 
                //         "%06X", objCode);
                snprintf(TRecord + TRecordCounter, 
                        currentLine->content.Instruction.isExtend ? 9 : 7, 
                        currentLine->content.Instruction.isExtend ? "%08X" : "%06X", 
                        objCode);
                TRecordCounter += currentLine->content.Instruction.isExtend ? 8 : 6;
            }
            // 處理BYTE指令
            else if (!strcmp(currentLine->content.Instruction.operation, "BYTE")) {
                if (currentLine->content.Instruction.operand[0] == 'C') {
                    char *start = strchr(currentLine->content.Instruction.operand, '\'') + 1;
                    char *end = strrchr(currentLine->content.Instruction.operand, '\'');
                    if (start && end && start != end) {
                        int length = end - start;
                        for (int i = 0; i < length; i++) {
                            snprintf(TRecord + TRecordCounter, 3, "%02X", (unsigned char)start[i]);
                            TRecordCounter += 2;
                        }
                    }
                } else if (currentLine->content.Instruction.operand[0] == 'X') {
                    char *start = strchr(currentLine->content.Instruction.operand, '\'') + 1;
                    char *end = strrchr(currentLine->content.Instruction.operand, '\'');
                    if (start && end && start != end) {
                        strncpy(TRecord + TRecordCounter, start, end - start);
                        TRecordCounter += end - start;
                    }
                }
            }
            // 處理WORD指令
            else if (!strcmp(currentLine->content.Instruction.operation, "WORD")) {
                int value = 0;
                int error = 0;
                value = evaluateExpression(currentLine->content.Instruction.operand, SYMTAB, &error);
                if (!error) {
                    snprintf(TRecord + TRecordCounter, 7, "%06X", value & 0xFFFFFF);
                    TRecordCounter += 6;
                }
            }
            // 處理BASE指令
            else if (!strcmp(currentLine->content.Instruction.operation, "BASE")) {
                if (searchHashTable(SYMTAB, currentLine->content.Instruction.operand, &foundSymValue)) {
                    SYMTABValue *symValue = (SYMTABValue *)foundSymValue;
                    interData->baseRegisterValue = symValue->address;
                }
            }
            // 處理END指令
            else if (!strcmp(currentLine->content.Instruction.operation, "END")) {
                break;
            }

            // 檢查是否需要輸出T記錄
            if (TRecordCounter > 0) {
                // 檢查是否需要開始新的T記錄
                if ((TRecordCounter >= 40) || 
                    (lineNumber < interData->lineNumber && 
                     (interData->lines[lineNumber]->content.Instruction.LOCATION - TRecordStart) > 0x1E)) {
                    
                    fprintf(objfile, "T%06X%02X%s\n", TRecordStart, TRecordCounter / 2, TRecord);
                    memset(TRecord, 0, sizeof(TRecord));
                    TRecordCounter = 0;
                    if (lineNumber < interData->lineNumber) {
                        TRecordStart = interData->lines[lineNumber]->content.Instruction.LOCATION;
                    }
                }
            }
        }
        // 處理註釋行
        else if (currentLine->type == COMMENT) {
            fprintf(lstfile, "%s\n", currentLine->content.lineComment);
        }
        // 處理空行
        else {
            fprintf(lstfile, "\n");
        }

        // 讀取下一行
        if (lineNumber <= interData->lineNumber) {
            currentLine = interData->lines[lineNumber++];
        }
    }

    // 輸出最後的T記錄（如果有的話）
    if (TRecordCounter > 0) {
        fprintf(objfile, "T%06X%02X%s\n", TRecordStart, TRecordCounter / 2, TRecord);
    }

    // 輸出E記錄
    fprintf(objfile, "E%06X\n", interData->startingAddress);
}


// 輔助函數：獲取寄存器編號
int getRegisterNum(char reg)
{
    switch(reg)
    {
        case 'A': return 0;
        case 'X': return 1;
        case 'L': return 2;
        case 'B': return 3;
        case 'S': return 4;
        case 'T': return 5;
        case 'F': return 6;
        default: return 0;
    }
}

LineData *readLine(FILE *file, int lineNumber)
{
    char line[MAX_LINE_LENGTH + 2];
    memset(line, '\0', sizeof(line));
    fgets(line, sizeof(line), file);
    if (strchr(line, '\n') == NULL && !feof(file))
    {
        printf("Warning: Line length exceeds limit at line %d. The maximum line length is 66.\n", lineNumber + 1);
        while (fgetc(file) != '\n' && !feof(file))
            ;
    }
    line[strcspn(line, "\n")] = '\0';

    LineData *parsedLine = (LineData *)calloc(1, sizeof(LineData));
    if (line[0] == '.')
    {
        strncpy(parsedLine->content.lineComment, line, MAX_LINE_LENGTH);
        parsedLine->content.lineComment[MAX_LINE_LENGTH] = '\0';
        parsedLine->type = COMMENT;
    }
    else if (strlen(line) > 1 && !isspace(line[9]))
    {
        copy_and_trim(parsedLine->content.Instruction.label, line, MAX_LABEL_LENGTH);
        copy_and_trim(parsedLine->content.Instruction.operation, line + 9, MAX_OPCODE_LENGTH);
        copy_and_trim(parsedLine->content.Instruction.operand, line + 17, MAX_OPERAND_LENGTH);
        copy_and_trim(parsedLine->content.Instruction.comment, line + 35, MAX_COMMENT_LENGTH);

        if (line[9] == '+')
        {
            parsedLine->content.Instruction.isExtend = true;
        }
        else
        {
            parsedLine->content.Instruction.isExtend = false;
        }

        if (strchr(parsedLine->content.Instruction.operand, ','))
        {
            parsedLine->content.Instruction.addrmode = ADDR_MODE_INDEX;
        }
        else if (line[17] == '#')
        {
            parsedLine->content.Instruction.addrmode = ADDR_MODE_IMMEDIATE;
        }
        else if (line[17] == '@')
        {
            parsedLine->content.Instruction.addrmode = ADDR_MODE_INDIRECT;
        }
        else
        {
            parsedLine->content.Instruction.addrmode = ADDR_MODE_RELATIVE;
        }
        parsedLine->type = INSTRUCTION;
    }
    else
    {
        parsedLine->type = EMPTY;
    }
    return parsedLine;
}

OPTABValue *createOPTABValue(int opcode, int format, bool isXE)
{
    OPTABValue *op = (OPTABValue *)malloc(sizeof(OPTABValue));
    op->opcode = opcode;
    op->format = format;
    op->isXE = isXE;
    return op;
}

SYMTABValue *createSYMTABValue(int address, int flag)
{
    SYMTABValue *sym = (SYMTABValue *)malloc(sizeof(SYMTABValue));
    sym->address = address;
    sym->flag = flag;
    return sym;
}

void setOPTAB(HashTable *OPTAB)
{
    insertHashTable(OPTAB, "ADD", createOPTABValue(0x18, 3, false));
    insertHashTable(OPTAB, "ADDR", createOPTABValue(0x90, 2, true));
    insertHashTable(OPTAB, "AND", createOPTABValue(0x40, 3, false));
    insertHashTable(OPTAB, "CLEAR", createOPTABValue(0xB4, 2, true));
    insertHashTable(OPTAB, "COMP", createOPTABValue(0x28, 3, false));
    insertHashTable(OPTAB, "COMPR", createOPTABValue(0xA0, 2, true));
    insertHashTable(OPTAB, "DIV", createOPTABValue(0x24, 3, false));
    insertHashTable(OPTAB, "DIVR", createOPTABValue(0x9C, 2, true));
    insertHashTable(OPTAB, "J", createOPTABValue(0x3C, 3, false));
    insertHashTable(OPTAB, "JEQ", createOPTABValue(0x30, 3, false));
    insertHashTable(OPTAB, "JGT", createOPTABValue(0x34, 3, false));
    insertHashTable(OPTAB, "JLT", createOPTABValue(0x38, 3, false));
    insertHashTable(OPTAB, "JSUB", createOPTABValue(0x48, 3, false));
    insertHashTable(OPTAB, "LDA", createOPTABValue(0x00, 3, false));
    insertHashTable(OPTAB, "LDB", createOPTABValue(0x68, 3, true));
    insertHashTable(OPTAB, "LDCH", createOPTABValue(0x50, 3, false));
    insertHashTable(OPTAB, "LDL", createOPTABValue(0x08, 3, false));
    insertHashTable(OPTAB, "LDS", createOPTABValue(0x6C, 3, true));
    insertHashTable(OPTAB, "LDT", createOPTABValue(0x74, 3, true));
    insertHashTable(OPTAB, "LDX", createOPTABValue(0x04, 3, false));
    insertHashTable(OPTAB, "MUL", createOPTABValue(0x20, 3, false));
    insertHashTable(OPTAB, "MULR", createOPTABValue(0x98, 2, true));
    insertHashTable(OPTAB, "OR", createOPTABValue(0x44, 3, false));
    insertHashTable(OPTAB, "RD", createOPTABValue(0xD8, 3, false));
    insertHashTable(OPTAB, "RMO", createOPTABValue(0xAC, 2, true));
    insertHashTable(OPTAB, "RSUB", createOPTABValue(0x4C, 3, false)); // star!!!!!!!!!!
    insertHashTable(OPTAB, "SHIFTL", createOPTABValue(0xA4, 2, true));
    insertHashTable(OPTAB, "SHIFTR", createOPTABValue(0xA8, 2, true));
    insertHashTable(OPTAB, "STA", createOPTABValue(0x0C, 3, false));
    insertHashTable(OPTAB, "STB", createOPTABValue(0x78, 3, true));
    insertHashTable(OPTAB, "STCH", createOPTABValue(0x54, 3, false));
    insertHashTable(OPTAB, "STL", createOPTABValue(0x14, 3, false));
    insertHashTable(OPTAB, "STS", createOPTABValue(0x7C, 3, true));
    insertHashTable(OPTAB, "STT", createOPTABValue(0x84, 3, true));
    insertHashTable(OPTAB, "STX", createOPTABValue(0x10, 3, false));
    insertHashTable(OPTAB, "SUB", createOPTABValue(0x1C, 3, false));
    insertHashTable(OPTAB, "SUBR", createOPTABValue(0x94, 2, true));
    insertHashTable(OPTAB, "TD", createOPTABValue(0xE0, 3, false));
    insertHashTable(OPTAB, "TIX", createOPTABValue(0x2C, 3, false));
    insertHashTable(OPTAB, "TIXR", createOPTABValue(0xB8, 2, true));
    insertHashTable(OPTAB, "WD", createOPTABValue(0xDC, 3, false));
}

void copy_and_trim(char *dest, const char *src, size_t len)
{
    size_t end = len;
    if (!src || !dest)
        return;
    while (end > 0 && isspace(src[end - 1]))
    {
        end--;
    }
    strncpy(dest, src, end);
    dest[end] = '\0';
}

char *concatenateStr(int numPairs, ...)
{
    if (numPairs <= 0)
    {
        fprintf(stderr, "Invalid input parameters.\n");
        return NULL;
    }
    va_list args;
    va_start(args, numPairs);
    size_t max_len = 0;

    va_list temp_args;
    va_copy(temp_args, args);
    int i;
    for (i = 0; i < numPairs; i++)
    {
        const char *str = va_arg(temp_args, const char *);
        int location = va_arg(temp_args, int);

        if (location < 0)
        {
            fprintf(stderr, "Invalid location: %d\n", location);
            va_end(temp_args);
            va_end(args);
            return NULL;
        }
        size_t str_len = strlen(str);
        if ((size_t)location + str_len > max_len)
            max_len = location + str_len;
    }
    va_end(temp_args);
    char *result = (char *)malloc((max_len + 1) * sizeof(char));
    if (!result)
    {
        fprintf(stderr, "Memory allocation failed\n");
        va_end(args);
        return NULL;
    }
    memset(result, ' ', max_len);
    result[max_len] = '\0';
    for (i = 0; i < numPairs; i++)
    {
        const char *str = va_arg(args, const char *);
        int location = va_arg(args, int);
        size_t str_len = strlen(str);
        strncpy(result + location, str, str_len);
    }
    va_end(args);
    return result;
}

bool isStandardSICCode(IntermediateData *interdata, HashTable *OPTAB)
{
    int i;
    void *foundValue;
    OPTABValue *opValue;
    for (i = 0; i < interdata->lineNumber; i++)
    {
        if (interdata->lines[i]->type == INSTRUCTION)
        {
            if (searchHashTable(OPTAB, interdata->lines[i]->content.Instruction.operand, &foundValue))
            {
                opValue = (OPTABValue *)foundValue;
                if (opValue->isXE)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

// Directive related functions
void setDirectiveTable(HashTable *DIRTAB)
{
    insertHashTable(DIRTAB, "ORG", createDirectiveValue(handleORG, DIR_ORG));
    insertHashTable(DIRTAB, "EQU", createDirectiveValue(handleEQU, DIR_EQU));
    insertHashTable(DIRTAB, "EXTREF", createDirectiveValue(handleEXTREF, DIR_EXTREF));
    insertHashTable(DIRTAB, "EXTDEF", createDirectiveValue(handleEXTDEF, DIR_EXTDEF));
    insertHashTable(DIRTAB, "CSECT", createDirectiveValue(handleCSECT, DIR_CSECT));
}

int handleORG(LineData *line, IntermediateData *data, HashTable *SYMTAB)
{
    char *endptr;
    int newLocation;
    
    // 處理表達式或直接地址
    if (strchr(line->content.Instruction.operand, '+') || strchr(line->content.Instruction.operand, '-'))
    {
        // TODO: 實作表達式計算
        // 這裡需要一個輔助函數來計算表達式
        newLocation = evaluateExpression(line->content.Instruction.operand, SYMTAB, NULL);
    }
    else
    {
        newLocation = (int)strtol(line->content.Instruction.operand, &endptr, 16);
        if (*endptr != '\0') {
            fprintf(stderr, "Error: Invalid hexadecimal string for ORG\n");
            return -1;
        }
        //  若 `operand` 含有不合法(非 0~9, A~F)字元或空字串，則可能會產生預期外行為。
    }
    
    if (newLocation < 0 || newLocation > 0x7FFF)
    {
        fprintf(stderr, "Error: Invalid ORG address\n");
        return -1;
    }
    
    return newLocation;
}

int handleEQU(LineData *line, IntermediateData *data, HashTable *SYMTAB)
{
    if (strlen(line->content.Instruction.label) == 0)
    {
        fprintf(stderr, "Error: EQU requires a label\n");
        return -1;
    }

    int error = 0;
    int value;
    
    // 處理特殊符號 * (當前位置計數器)
    if (strcmp(line->content.Instruction.operand, "*") == 0)
    {
        value = line->content.Instruction.LOCATION;
    } else {
        value = evaluateExpression(line->content.Instruction.operand, SYMTAB, &error);
        if (error != 0) {
            return -1;
        }
    }

    // 檢查符號是否已存在
    void *existingValue;
    if (searchHashTable(SYMTAB, line->content.Instruction.label, &existingValue)) {
        fprintf(stderr, "Error: Symbol '%s' is already defined\n", 
                line->content.Instruction.label);
        return -1;
    }

    // 將符號加入符號表
    insertHashTable(SYMTAB, line->content.Instruction.label, createSYMTABValue(value, 0));
    return value;
}

int handleEXTREF(LineData *line, IntermediateData *data, HashTable *SYMTAB)
{
    char *token = strtok(line->content.Instruction.operand, ",");
    while (token != NULL)
    {
        // 移除前後空格
        while (isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) end--;
        *(end + 1) = '\0';

        // 添加到外部引用表
        ExternalSymbol *newRef = &data->sections[data->currentSection].externals[
            data->sections[data->currentSection].externalCount++];
        strncpy(newRef->name, token, MAX_LABEL_LENGTH);
        newRef->isRef = true;

        token = strtok(NULL, ",");
    }
    return 0;
}

int handleEXTDEF(LineData *line, IntermediateData *data, HashTable *SYMTAB)
{
    // 類似 EXTREF 的處理，但設置 isRef = false
    char *token = strtok(line->content.Instruction.operand, ",");
    while (token != NULL)
    {
        while (isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) end--;
        *(end + 1) = '\0';
        ExternalSymbol *newDef = &data->sections[data->currentSection].externals[
            data->sections[data->currentSection].externalCount++];
        strncpy(newDef->name, token, MAX_LABEL_LENGTH);
        newDef->isRef = false;

        token = strtok(NULL, ",");
    }
    return 0;
}

int handleCSECT(LineData *line, IntermediateData *data, HashTable *SYMTAB)
{
    // 結束當前段並開始新段
    if (data->currentSection >= 0)
    {
        data->sections[data->currentSection].length = 
            line->content.Instruction.LOCATION - data->sections[data->currentSection].startAddress;
    }

    // 創建新的控制段
    data->currentSection = data->sectionCount++;
    ControlSection *newSection = &data->sections[data->currentSection];
    strncpy(newSection->name, line->content.Instruction.label, MAX_LABEL_LENGTH);
    newSection->startAddress = line->content.Instruction.LOCATION;
    newSection->externalCount = 0;
    newSection->externals = (ExternalSymbol *)calloc(50, sizeof(ExternalSymbol)); // 假設最多50個外部符號

    // 將控制段名稱加入符號表
    insertHashTable(SYMTAB, line->content.Instruction.label, 
                   createSYMTABValue(line->content.Instruction.LOCATION, 0));

    return line->content.Instruction.LOCATION;
}

// 實作 evaluateExpression 及其輔助函數
int evaluateExpression(const char *expression, HashTable *SYMTAB, int *error) {
    if (!expression || !*expression) {
        fprintf(stderr, "Error: Empty expression\n");
        return 0;
    }
    
    // 跳過開頭的空白
    while (isspace(*expression)) expression++;
    
    int result = evaluateTerm(&expression, SYMTAB, error);
    if (*error != 0) return 0;
    
    while (*expression) {
        while (isspace(*expression)) expression++;
        
        char operator = *expression;
        if (operator != '+' && operator != '-') {
            break;
        }
        
        expression++;  // 移到下一個字符
        int term = evaluateTerm(&expression, SYMTAB, error);
        if (*error != 0) return 0;
        
        // 檢查溢出
        if (operator == '+') {
            result += term;
            if (!checkAddressOverflow(result)) {
                fprintf(stderr, "Error: Address overflow in addition\n");
                *error = EXPR_OVERFLOW;
                return 0;
            }
        } else {  // operator == '-'
            result -= term;
            if (!checkAddressOverflow(result)) {
                fprintf(stderr, "Error: Address underflow in subtraction\n");
                *error = EXPR_OVERFLOW;
                return 0;
            }
        }
    }
    
    // 檢查是否有多餘字元
    while (isspace(*expression)) expression++;
    if (*expression != '\0') {
        fprintf(stderr, "Error: Extra characters after expression: %s\n", expression);
        *error = EXPR_ERROR;
        return 0;
    }
    
    return result;
}

int evaluateTerm(const char **expression, HashTable *SYMTAB, int *error) {
    int result = evaluateFactor(expression, SYMTAB, error);
    if (*error != 0) return 0;
    
    while (**expression) {
        while (isspace(**expression)) (*expression)++;
        
        char operator = **expression;
        if (operator != '*' && operator != '/') {
            break;
        }
        
        (*expression)++;  // 移到下一個字符
        int factor = evaluateFactor(expression, SYMTAB, error);
        if (*error != 0) return 0;
        
        if (operator == '*') {
            result *= factor;
            if (!checkAddressOverflow(result)) {
                fprintf(stderr, "Error: Address overflow in multiplication\n");
                *error = EXPR_OVERFLOW;
                return 0;
            }
        } else {  // operator == '/'
            if (factor == 0) {
                fprintf(stderr, "Error: Division by zero\n");
                *error = EXPR_ERROR;
                return 0;
            }
            result /= factor;
        }
    }
    
    return result;
}

int evaluateFactor(const char **expression, HashTable *SYMTAB, int *error) {
    while (isspace(**expression)) (*expression)++;
    
    // 處理負號
    bool isNegative = false;
    if (**expression == '-') {
        isNegative = true;
        (*expression)++;
        while (isspace(**expression)) (*expression)++;
    }
    
    // 處理括號
    if (**expression == '(') {
        (*expression)++;  // 跳過左括號
        int result = evaluateExpression(*expression, SYMTAB, error);
        if (*error != 0) return 0;
        
        // 尋找對應的右括號
        int bracketCount = 1;
        while (**expression) {
            if (**expression == '(') bracketCount++;
            else if (**expression == ')') bracketCount--;
            
            if (bracketCount == 0) break;
            (*expression)++;
        }
        
        if (**expression != ')' || bracketCount != 0) {
            fprintf(stderr, "Error: Mismatched parentheses\n");
            *error = EXPR_ERROR;
            return 0;
        }
        (*expression)++;  // 跳過右括號
        
        return isNegative ? -result : result;
    }
    
    // 處理數字
    if (isdigit(**expression)) {
        char *endptr;
        int value;
        
        // 檢查是否為十六進制數
        if (**expression == '0' && (*(*expression + 1) == 'x' || *(*expression + 1) == 'X')) {
            value = strtol(*expression, &endptr, 16);
        } else {
            value = strtol(*expression, &endptr, 10);
        }
        
        if (endptr == *expression) {
            fprintf(stderr, "Error: Invalid number format\n");
            *error = EXPR_ERROR;
            return 0;
        }
        
        *expression = endptr;
        value = isNegative ? -value : value;
        
        if (!checkAddressOverflow(value)) {
            fprintf(stderr, "Error: Number out of valid address range\n");
            *error = EXPR_OVERFLOW;
            return 0;
        }
        
        return value;
    }
    
    // 處理符號
    if (isalpha(**expression) || **expression == '_') {
        char symbol[MAX_LABEL_LENGTH + 1] = {0};
        int i = 0;
        
        while (isalnum(**expression) || **expression == '_') {
            if (i < MAX_LABEL_LENGTH) {
                symbol[i++] = **expression;
            }
            (*expression)++;
        }
        
        void *foundValue;
        if (searchHashTable(SYMTAB, symbol, &foundValue)) {
            SYMTABValue *value = (SYMTABValue *)foundValue;
            int result = value->address;
            
            // 檢查符號是否已定義
            if (value->flag == 1) {  // 假設 flag == 1 表示重複定義
                fprintf(stderr, "Error: Symbol '%s' is multiply defined\n", symbol);
                *error = EXPR_ERROR;
                return 0;
            }
            
            result = isNegative ? -result : result;
            if (!checkAddressOverflow(result)) {
                fprintf(stderr, "Error: Symbol '%s' value out of range\n", symbol);
                *error = EXPR_OVERFLOW;
                return 0;
            }
            
            return result;
        } else {
            fprintf(stderr, "Error: Undefined symbol '%s'\n", symbol);
            *error = EXPR_ERROR;
            return 0;
        }
    }
    
    fprintf(stderr, "Error: Invalid expression\n");
    *error = EXPR_ERROR;
    return 0;
}

// 檢查地址是否在有效範圍內
bool checkAddressOverflow(int value) {
    return (value >= 0 && value <= 0x7FFF);  // SIC/XE 15-bit address space
}
