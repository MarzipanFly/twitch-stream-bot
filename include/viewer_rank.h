#ifndef VIEWER_RANK_H
#define VIEWER_RANK_H

typedef struct
{
	const char *name;
	long long minimum_balance;
} ViewerRank;

/*
 * Возвращаем звание потекущему балансу
 */
const ViewerRank *viewer_rank_get(
		long long balance
);

/*
 * Возвращает следующее звание.
 * NULL означает максимальный ранг
 */
const ViewerRank *viewer_rank_next(
		long long balance
);

#endif // VIEWER_RANK_H
