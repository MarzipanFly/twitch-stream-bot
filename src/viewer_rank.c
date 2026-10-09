#include "viewer_rank.h"

#include <stddef.h>

static const ViewerRank ranks[] =
{
	{"Новичок", 0},
	{"Собираетль", 100},
	{"Апельсиновод", 500},
	{"Цитрусовый магнат", 2000},
	{"Апельсиновый король", 10000}
};

#define RANK_COUNT (sizeof(ranks) / sizeof(ranks[0]))

const ViewerRank *viewer_rank_get(
		long long balance
)
{
	size_t i;

	const ViewerRank *current = &ranks[0];

	for (i=0; i < RANK_COUNT; ++i)
	{
		if (balance < ranks[i].minimum_balance)
		{
			break;
		}

		current = &ranks[i];
	}

	return current;
}

const ViewerRank *viewer_rank_next(
		long long balance
)
{
	size_t i;

	for (i=0; i < RANK_COUNT; ++i)
	{
		if (balance < ranks[i].minimum_balance)
		{
			return &ranks[i];
		}
	}

	return NULL;
}
