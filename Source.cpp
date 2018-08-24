
//--------------------------------------------------
// インクルード
//--------------------------------------------------
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "clHCA.h"
#include "HCADecodeService.h"

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_DEPRECATE
#endif

//--------------------------------------------------
// メイン
//--------------------------------------------------
int main(int argc, char *argv[]) {

    // コマンドライン解析
    unsigned int count = 0;
    char *filenameOut = NULL;
    //bool decodeFlg=false;
    float volume = 1;
    unsigned int ciphKey1 = 0xBC731A85;
    unsigned int ciphKey2 = 0x0002B875;
    int mode = 16;
    int loop = 0;
    bool info = false;
    bool decrypt = false;
    for (int i = 1; i<argc; ++i) {
        if (argv[i][0] == '-' || argv[i][0] == '/') {
            switch (argv[i][1]) {
            case 'o':if (i + 1<argc) { filenameOut = argv[++i]; }break;
                //case 'd':decodeFlg=true;break;
            case 'v':volume = (float)atof(argv[++i]); break;
            case 'a':if (i + 1<argc) { ciphKey1 = strtoul(argv[++i], NULL, 16); }break;
            case 'b':if (i + 1<argc) { ciphKey2 = strtoul(argv[++i], NULL, 16); }break;
            case 'm':if (i + 1<argc) { mode = atoi(argv[++i]); }break;
            case 'l':if (i + 1<argc) { loop = atoi(argv[++i]); }break;
            case 'i':info = true; break;
            case 'c':decrypt = true; break;
            }
        }
        else if (*argv[i]) {
            argv[count++] = argv[i];
        }
    }

    //if(decodeFlg){

    // 入力チェック
    if (!count) {
        printf("Error: 入力ファイルを指定してください。\n");
        return -1;
    }

	HCADecodeService dec{}; // Start decode service
	std::pair<std::string, std::pair<void*, size_t>>* fileslist = new std::pair<std::string, std::pair<void*, size_t>>[count];

    // デコード
    for (unsigned int i = 0; i<count; ++i) {

        // 2つ目以降のファイルは、出力ファイル名オプションが無効
        if (i)filenameOut = NULL;

        // デフォルト出力ファイル名
        char path[MAX_PATH];
        if (!(filenameOut&&filenameOut[0])) {
			path[0] = '\0';
            strncat(path, argv[i], sizeof(path) - 1);
            char *d1 = strrchr(path, '\\');
            char *d2 = strrchr(path, '/');
            char *e = strrchr(path, '.');
            if (e && d1 < e && d2 < e)*e = '\0';
            strcat(path, ".wav");
            filenameOut = path;
        }

        // ヘッダ情報のみ表示
        if (info) {
            printf("%s のヘッダ情報\n", argv[i]);
            clHCA hca(0, 0);
            hca.PrintInfo(argv[i]);
            printf("\n");
        }

        // 復号化
        else if (decrypt) {
            printf("%s を復号化中...\n", argv[i]);
            clHCA hca(ciphKey1, ciphKey2);
            if (!hca.Decrypt(argv[i])) {
                printf("Error: 復号化に失敗しました。\n");
            }
        }

        // デコード
        else {
            printf("%s をデコード中...\n", argv[i]);
            auto wavout = dec.decode(argv[i], 0, ciphKey1, ciphKey2, volume, mode, loop);
            if (!wavout.first)
            {
                printf("Error: デコードに失敗しました。\n");
            }
			else
			{
				fileslist[i] = std::make_pair(std::string(filenameOut), wavout);
			}
        }
    }

	for (unsigned int i = 0; i < count; ++i)
	{
		printf("%s を書き込み中...\n", fileslist[i].first.c_str());
		FILE* outfile = fopen(fileslist[i].first.c_str(), "wb");
		if (!outfile)
		{
			printf("Error: WAVEファイルの作成に失敗しました。\n");
			dec.cancel_decode(fileslist[i].second.first);
			free(fileslist[i].second.first);
		}
		else
		{
			dec.wait_on_request(fileslist[i].second.first);
			fwrite(fileslist[i].second.first, 1, fileslist[i].second.second, outfile);
			free(fileslist[i].second.first);
			fclose(outfile);
		}
	}

    //}

	delete[] fileslist;

    return 0;
}
