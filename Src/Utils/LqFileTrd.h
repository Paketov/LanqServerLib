/*
* Lanq(Lan Quick)
* Solodov A. N. (hotSAN)
* 2016
*   LqFileTrd... - (Lanq File Transaction) Implements transactions for correct saving file in os fs.
*/


#ifndef __TRANSACTED_FILE_H_HAS_INCLUDED__
#define __TRANSACTED_FILE_H_HAS_INCLUDED__
#include <stdint.h>

#define LQ_TC_REPLACE		0x00010000
#define LQ_TC_SUBDIR		0x00020000		

LQ_EXTERN_C_BEGIN

LQ_IMPORTEXPORT int LQ_CALL LqFileTrdCreate(const char* NameFinnalyDest, uint32_t Flags, int Access);
LQ_IMPORTEXPORT int LQ_CALL LqFileTrdCancel(int Fd);
LQ_IMPORTEXPORT int LQ_CALL LqFileTrdCommit(int Fd);
LQ_IMPORTEXPORT int LQ_CALL LqFileTrdCommitToPlace(int Fd, const char* DestPath);
LQ_IMPORTEXPORT int LQ_CALL LqFileTrdGetNameTemp(int Fd, char* DestBuf, size_t DestBufLen);
LQ_IMPORTEXPORT int LQ_CALL LqFileTrdGetNameTarget(int Fd, char* DestBuf, size_t DestBufLen);

LQ_EXTERN_C_END
#endif