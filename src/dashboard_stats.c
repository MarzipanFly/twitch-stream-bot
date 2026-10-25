#include "dashboard_stats.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void dashboard_chart_observe(DashboardChart *chart,const TwitchStream *stream)
{
    if(!chart||!stream)return;
    if(!stream->is_live)
    {
        /* Leave final samples visible until the next stream. */
        return;
    }
    if(strcmp(chart->stream_id,stream->id))
    {
        memset(chart,0,sizeof(*chart));
        snprintf(chart->stream_id,sizeof(chart->stream_id),"%s",stream->id);
    }
    if(chart->count==DASH_CHART_SAMPLES)
    {
        memmove(chart->viewers,chart->viewers+1,
                sizeof(int)*(DASH_CHART_SAMPLES-1));
        --chart->count;
    }
    chart->viewers[chart->count++]=stream->viewer_count>=0?
                                   stream->viewer_count:0;
}
static int id_ok(const char *id)
{
    size_t i,n=id?strlen(id):0;
    if(!n||n>=TWITCH_STREAM_ID_SIZE)return 0;
    for(i=0;i<n;++i)if(id[i]<'0'||id[i]>'9')return 0;
    return 1;
}
int dashboard_mark_moment(const TwitchStream *stream,const char *note,
                          time_t now,char *message,size_t size)
{
    char path[400],title[420],*p; FILE *f;
    time_t started=0;
    struct tm tm_start;
    int seconds=-1,ok;
    if(message && size)message[0]='\0';
    if(!stream || !stream->is_live || !id_ok(stream->id) ||
       !message || size<60)return 0;
    memset(&tm_start,0,sizeof(tm_start));
    if(strlen(stream->started_at)>=19 &&
       sscanf(stream->started_at,"%d-%d-%dT%d:%d:%d",
           &tm_start.tm_year,&tm_start.tm_mon,&tm_start.tm_mday,
           &tm_start.tm_hour,&tm_start.tm_min,&tm_start.tm_sec)==6)
    {
        tm_start.tm_year-=1900;tm_start.tm_mon--;
        /* Twitch started_at is UTC; _mkgmtime converts the UTC tm. */
        started=_mkgmtime(&tm_start);
        if(started>0 && now>=started)seconds=(int)(now-started);
    }
    snprintf(path,sizeof(path),"data/stream_reports/stream_%s_markers.tsv",
             stream->id);
    snprintf(title,sizeof(title),"%s",note&&*note?note:"Интересный момент");
    for(p=title;*p;++p)if(*p=='\r'||*p=='\n'||*p=='\t')*p=' ';
    f=fopen(path,"ab");
    if(!f)return 0;
    ok=fprintf(f,"%lld\t%d\t%s\n",(long long)now,seconds,title)>0;
    if(fclose(f)!=0)ok=0;
    if(ok)snprintf(message,size,"Момент отмечен: %d:%02d — %s",
                    seconds>=0?seconds/60:0,seconds>=0?seconds%60:0,title);
    return ok;
}
size_t dashboard_report_history(char *output,size_t capacity)
{
    WIN32_FIND_DATAA find;
    HANDLE h;
    size_t used=0,count=0;
    if(!output||capacity<50)return 0;
    output[0]='\0';
    h=FindFirstFileA("data/stream_reports/*_summary.txt",&find);
    if(h==INVALID_HANDLE_VALUE)return 0;
    do {
        char path[400],line[500],name[80]="",peak[80]="",duration[80]="";
        FILE *file;
        if(count>=8)break;
        snprintf(path,sizeof(path),"data/stream_reports/%s",find.cFileName);
        file=fopen(path,"rb");
        if(!file)continue;
        while(fgets(line,sizeof(line),file))
        {
            if(!strncmp(line,"Stream ID:",10))
                snprintf(name,sizeof(name),"%.60s",line+11);
            if(!strncmp(line,"Peak concurrent viewers OBSERVED:",33))
                snprintf(peak,sizeof(peak),"%.60s",line+34);
            if(!strncmp(line,"Approximate bot-observed duration:",34))
                snprintf(duration,sizeof(duration),"%.60s",line+35);
        }
        fclose(file);
        name[strcspn(name,"\r\n")]='\0';
        peak[strcspn(peak,"\r\n")]='\0';
        duration[strcspn(duration,"\r\n")]='\0';
        {
            int n=snprintf(output+used,capacity-used,
                           "%s • пик %s • %s мин\n",name,
                           peak[0]?peak:"?",duration[0]?duration:"?");
            if(n<0||(size_t)n>=capacity-used)break;
            used+=(size_t)n;++count;
        }
    } while(FindNextFileA(h,&find));
    FindClose(h);
    return count;
}
