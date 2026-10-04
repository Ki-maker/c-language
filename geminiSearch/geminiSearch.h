#ifndef GEMINI_SEARCH_H
#define GEMINI_SEARCH_H

/* 入力キーワードが呼び出し条件(最小文字数)を満たすか. 1: 満たす, 0: 満たさない */
int isGeminiKeywordEligible(const char *keyword);

/*
 * Gemini APIで検索候補を取得し、文字列配列のJSON(例: ["a","b"])を返す.
 * 呼び出し元がfree()する. 失敗時はNULL.
 */
char *getGeminiSuggestions(const char *keyword);

#endif
