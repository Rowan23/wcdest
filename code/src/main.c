#include <stdio.h>



 
/* 	$Id: wcd.c,v 0.5 2009/04/02 15:02:43 scott Exp scott $	 */

#ifndef lint
static char vcid[] = "$Id: wcd.c,v 0.5 2009/04/02 15:02:43 scott Exp scott $";
#endif /* lint */



/*--

  wcd.c    does EST clustering

  Copyright (C) Scott Hazelhurst     2003-2008
  School of Computer Science, 
  University of the Witwatersrand,
  Johannesburg
  Private Bag 3, 2050 Wits
  South Africa    
  Scott.Hazelhurst@wits.ac.za

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public Licence
  as published by the Free Software Foundation; either version 2
  of the Licence, or (at your option) any later version.
     
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public Licence for more details.

  http://code.google.com/p/wcdest


*/




#include "wcd.h"
#ifndef __COMMONINCL
#include "common.h"
#endif
#include "d2.h"
#include "ed.h"
#include "auxcluster.h"
#include <strings.h>
#include "suffixcluster.h"
#include <string.h>

#include "mpistuff.h"


#ifdef PTHREADS
#include "pthread.h"
#include "math.h"
#include "pthreadstuff.h"
#endif


// Imported from common

extern int  tr[4];  // see note below -- for translating input
extern int * rc , * rc_big; // table for RC
extern wordElt * word1, *word2;
extern int alpha;
extern int beta;
extern int ignore_active;
extern int sample_thresh;
extern int sp, sr;
extern int num_words_win;      // number of words in window
extern int theta; // default value
extern int rc_check;
extern int window_len;
extern int word_len;
extern int word_tsize;  // 4^word_len
extern int word_mask;   // word_tszie1
extern int word_shift;  // 2*(word_len-1)
extern int NM_threshold;
extern int skip;
extern int NUM_num_matches;
extern int NUM_dfn;
extern int NUM_dfn_succ;
extern int NUM_full_heuristic;
extern int total_matches;
extern int num_seqs;
extern int myid;
extern int numprocs;
extern int suffix_len;
extern SeqPtr * seq;   // array for each sequence
extern SeqPtr data;    // the whole lot seq[0]==data
extern SeqInfoPtr seqInfo;
extern SeqIDPtr   seqID;
extern UnionFindPtr tree;
extern int num_threads;
extern int clonelink; // is clone linking done?
extern int reindex_value;

int data_size;

#define MAXBLOCK 1000





typedef void (*clusterFunType)(WorkPtr work);

clusterFunType do_cluster;

WorkPtr   work;

FILE     * outf, * dump_file, * checkfile=(FILE *) 0;


//  These variables described the workload of this process
//  They are set either by the user directly, or indirectly
//  through the mpi_get_task function which allocates the work
//  load.
int global_i_beg, global_i_end, global_j_beg, global_j_end;






// Global variable
ProgOptionsType prog_opts;


#ifdef NOLONGOPT
#define GETOPT(argc,argv,allowed_options,long_options,n) getopt(argc,argv,allowed_options)
#else
#include <getopt.h>
static struct option long_options[] = {
  {"abbrev_compare",0,0,'e'},
  {"alpha",1,0,'@'},
  {"add",0,0,'a'},
  {"beta",1,0,'$'},
  {"cluster_compare",0,0,'D'},
  {"clonelink",0,0,'X'},
  {"common_word",1,0,'H'},
  {"compare",0,0,'E'},
  {"splitup",1,0,'k'},
  {"constraint1",1,0,'j'},
  {"constraint2",1,0,'J'},
  {"dump",1,0,'d'},
  {"function",1,0,'F'},
  {"produce_clusters",1,0,'G'},
  {"histogram",0,0,'g'},
  {"init_cluster",1,0,'f'},
  {"checkpoint",1,0,'L'},
  {"merge",0,0,'m'},
  {"no_rc",0,0,'n'},
  {"num_seqs", 1, 0, 'C'},
  {"num_threads",1,0, 'N'},
  {"output",1,0,'o'},
  {"pairwise",0,0,'p'},
  {"parameter",1,0,'P'},
  {"reindex",1,0,'Q'},
  {"performance",0,0,'s'},
  {"range",0,0,'R'},
  {"restore",0,0,'A'},
  {"recluster",1,0,'r'},
  {"sample_thresh", 1, 0, 'K'},
  {"show_clusters", 0, 0, 'c'},
  {"show_ext",0,0,'t'},
  {"show_rc_seq",1,0,'I'},
  {"show_seq",1,0,'i'},
  {"skip",1,0,'S'},
  {"threshold",1,0,'T'},
  {"window_len",1,0,'l'},
  {"word_len",1,0,'w'},
  {"suffix_len",1,0,'U'},
  {"version",0,0,'v'},
  {"noninterleavednlc",0,0,'z'},
  {0, 0, 0, 0}
};

#define GETOPT(argc,argv,allowed_options,long_options,n) getopt_long(argc,argv,allowed_options, long_options,0)

#endif



static char * allowed_options = "@:$:AC:DEF:G:J:H:I:K:L:MN:P:Q:RS:VXT:Zacd:ef:ghi:j:k:l:mno:pr:stU:vw:xz";



int true_function(WorkPtr work, int i, int j, int k) {
  return 1;
}




string word2str(wordElt j, int n) {
  // given a number representing an n-word
  // return its string
  int k;
  char * ans;
  ans = (char *) malloc((size_t) n+1);
  strcpy(ans,"");
  for(k=0; k<n; k++) {
    ans[k] = cod2ch((int) (j&3));
    j = j>> 2;
  }
  ans[n]= (char) 0;
  return ans;
}



void pseq(uint i) {  // depbugging code only
  // prints out the sequence (since sequences are stored in
  // compressed form, can't just do a printf!
  int k;
  int  w=(SEQELTWIDTH/2);
  for(k=0; k<seqInfo[i].len; k++) {
    fprintf(outf,"%c",cod2ch(seq[i][k/w]>>((k%w)*2)&3));
  }
  fprintf(outf,"\n");
}




void pseqi(uint i) {  // depbugging code only
  // prints out the RC of the sequence
  int k;
  int  w=(SEQELTWIDTH/2);
  
  for(k=seqInfo[i].len-1; k>=0; k--) {
    fprintf(outf,"%c",cod2ch((~seq[i][k/w])>>((k%w)*2)&3));
  }
  fprintf(outf,"\n");
}




// Do clone linking


void clone_link(WorkPtr work) {
  int r, s;
  int i, j, rootI, rootJ, *index;
  index = work->index;

#ifndef NOAUXINFO

  for(r=work->i_beg; r<work->i_end; r++) {
    i = (index == NULL) ? r : index[r];
    if (IGNORE_SEQ(i)) continue;
    if (seqInfo[i].len<window_len) continue;
    for(s=MAX(r+1,work->j_beg); s<work->j_end; s++) {
      j = (index == NULL) ? s : index[s];
      rootI = mini_find_parent(i);
      rootJ = mini_find_parent(j);
      if ((rootI != rootJ) && !(IS_FLAG(FIX,rootI)&&IS_FLAG(FIX,rootJ) )) 
	if ((strlen(seqID[i].clone)>0) &&
	    (strlen(seqID[j].clone)>0) &&
	    (strcmp(seqID[i].clone,seqID[j].clone)==0)) {
	  make_uniond2(rootI,rootJ,0);
	}
    }
  }

  PLOCK(&find_parent_mutex);
  for(i=0; i<num_seqs; i++) 
    tree[i].cluster = find_parent(i);
  PUNLOCK(&find_parent_mutex);
#endif

}	  


//------------ key clustering algorithm



inline
void check_heuristics(int i, int j, int limit, int * num_mat_pos, int * num_mat_rc ) {
	int k;
	int samp_pos, samp_rc;
	uint16_t w0;
	
	*num_mat_pos=*num_mat_rc=samp_pos=samp_rc=0;
				

	for(k=1; k < limit ;k=k+1) {
		if (work->tableP[seq[j][k]]) samp_pos ++; 
		if (work->tableR[seq[j][k]]) samp_rc ++;
	}
	
	if ((samp_pos<=2) && (samp_rc<= 2) )
		samp_pos=samp_rc=0;
	else
		for(k=0; k < limit;k=k+2) {
			w0  =  seq[j][k];
			if (work->tableP[w0]) samp_pos++;
			if (work->tableR[w0]) samp_rc++;
		}
	if (samp_pos >sample_thresh)
		*num_mat_pos = tv_heuristic_pos(work,i,j);
	if (samp_rc > sample_thresh) 
		*num_mat_rc = tv_heuristic_rc(work,i,j);	
}


void merge_pair(WorkPtr work, int rootI, int i, int rootJ, int j, int dir) {
	PLOCK(&invert_mutex);
	short invert;
	invert = (int) (tree[i].orient !=  tree[j].orient) ^ dir;
	PUNLOCK(&invert_mutex);
	if (dump_file != NULL) {
		PLOCK(&dump_mutex);
		fprintf(dump_file,"%d: %d %d %d\n",
				work->thread_num,i,j,1);
		fflush(dump_file);
		PUNLOCK(&dump_mutex);
	}
	make_uniond2(rootI,rootJ,invert);
	total_matches++;
	tree[i].match = j;
	if (tree[j].match==-1) {
		tree[j].match = i;
	}
}

void complete_pairwise_cluster(WorkPtr work, int i, int * candidates, int num_cands) {
	int  s, limit;
	int j, rootI, rootJ;

	int num_mat_pos, num_mat_rc;

	if (clonelink) clone_link(work);
		
	if (IGNORE_SEQ(i)) return;
	if (seqInfo[i].len<window_len) return;

	set_up_word_table(work, i);

	for(s=0; s<num_cands; s++) {
		j = candidates[s];
        //printf("    Candidated %d\n",j);
		rootI = mini_find_parent(i);
		rootJ = mini_find_parent(j);
		assert(i<num_seqs);
		if ((rootI != rootJ) && !(IS_FLAG(FIX,rootI)&&IS_FLAG(FIX,rootJ) )) { 
			// only try to cluster if not already clustered
			// and both are not fixed lcusters
			NUM_num_matches++;
			limit = seqInfo[j].len/BASESPERELT-1;

			check_heuristics(i,j,limit,&num_mat_pos,&num_mat_rc);

			if( (num_mat_pos >= NM_threshold) &&  
			   (!IGNORE_SEQ(j)) &&
			   (d2(work,i,j,0)<=theta)) {
				merge_pair(work,rootI,i,rootJ,j,0);
			};

			if ( rc_check &&   
				(num_mat_rc >= NM_threshold)&& 
				(!IGNORE_SEQ(j)) &&
				(d2(work,i,j,1)<=theta)){
				merge_pair(work,rootI,i,rootJ,j,1);
			}
 
		}
	}
	clear_word_table(work,i);
	PLOCK(&find_parent_mutex);
	for(s=0; s<num_cands; s++) 
		tree[candidates[s]].cluster = find_parent(candidates[s]);
	PUNLOCK(&find_parent_mutex);

}



void do_pairwise_cluster(WorkPtr work) {
  int r, s, limit;
  int i, j, rootI, rootJ, *index,k;
  uint16_t w0;
  int num_mat_pos, num_mat_rc,invert, samp_pos, samp_rc;

  index = work->index;
  if (clonelink) clone_link(work);
  //printf("Slave %d-t%d here ib=%d ie=%d;  jb=%d je=%d,\n",
  // myid,work->thread_num,work->i_beg,work->i_end,work->j_beg,work->j_end);
  for(r=work->i_beg; r<work->i_end; r++) {
    i = (index == NULL) ? r : index[r];
    //printf("%d %ld %ld %d\n",i,data,seq[i].seq,seq[i].len);
    if (IGNORE_SEQ(i)) continue;
    if (seqInfo[i].len<window_len) continue;
    set_up_word_table(work, i);
    for(s=MAX(r+1,work->j_beg); s<work->j_end; s++) {
      j = (index == NULL) ? s : index[s];
      rootI = mini_find_parent(i);
      rootJ = mini_find_parent(j);
      if ((rootI != rootJ) && !(IS_FLAG(FIX,rootI)&&IS_FLAG(FIX,rootJ) )) { 
        // only try to cluster if not already clustered
        // and both are not fixed lcusters
	num_mat_pos=num_mat_rc=samp_pos=samp_rc=0;
	NUM_num_matches++;
	limit = seqInfo[j].len/BASESPERELT-1;
	for(k=1; k < limit ;k=k+2) {
	  if (work->tableP[seq[j][k]]) samp_pos ++; 
	  if (work->tableR[seq[j][k]]) samp_rc ++;
	}
	if ((samp_pos<=2) && (samp_rc<= 2) )
	  samp_pos=samp_rc=0;
	else
	  for(k=0; k < limit;k=k+2) {
	    w0  =  seq[j][k];
	    if (work->tableP[w0]) samp_pos++;
	    if (work->tableR[w0]) samp_rc++;
	  }
	if (samp_pos >sample_thresh)
	  num_mat_pos = tv_heuristic_pos(work,i,j);
	if (samp_rc > sample_thresh) 
	  num_mat_rc = tv_heuristic_rc(work,i,j);
	if( (num_mat_pos >= NM_threshold) &&  
            (!IGNORE_SEQ(j)) &&
            (dist(work,i,j,0)<=theta)) {
	  PLOCK(&invert_mutex);
	  invert = (int) (tree[i].orient !=  tree[j].orient);
          PUNLOCK(&invert_mutex);
          if (dump_file != NULL) {
	    PLOCK(&dump_mutex);
	    fprintf(dump_file,"%d: %d %d %d\n",
		    work->thread_num,i,j,1);
	    fflush(dump_file);
	    PUNLOCK(&dump_mutex);
	  }
	  make_uniond2(rootI,rootJ,invert);
	  total_matches++;
	  tree[i].match = j;
          if (tree[j].match==-1) {
	    tree[j].match = i;
	  }
	};
	if ( rc_check &&   
             (num_mat_rc >= NM_threshold)&& 
             (!IGNORE_SEQ(j)) &&
	     (dist(work,i,j,1)<=theta)){
	  PLOCK(&invert_mutex);
	  invert = (tree[i].orient==tree[j].orient);
	  PUNLOCK(&invert_mutex);
	  if (dump_file != NULL) {
	    PLOCK(&dump_mutex);
	    fprintf(dump_file,"%d: %d %d %d\n",
		    work->thread_num,i,j,-1);
	    fflush(dump_file);
	    PUNLOCK(&dump_mutex);
	  }
	  make_uniond2(rootI,rootJ,invert);
	  total_matches++;
	  tree[i].match = j;
	  if (tree[j].match==-1) {
	    tree[j].match = i;
	  }
	} 
      }
    }
    clear_word_table(work,i);
  }
  PLOCK(&find_parent_mutex);
  for(i=0; i<num_seqs; i++) 
    tree[i].cluster = find_parent(i);
  PUNLOCK(&find_parent_mutex);

}




void perform_clustering(WorkPtr work) {

#ifdef MPI
  if ((numprocs > 1) && (myid == 0)) {
    do_MPImaster_cluster(work);
    return;
  }
#endif

  // handle suffix clustering
  if (do_cluster == do_suffix_cluster) {
    work->i_beg=global_i_beg;
    work->i_end=global_i_end;
    work->j_beg=global_j_beg;
    work->j_end=global_j_end;
    do_cluster(work);
    return;
  }

#ifdef PTHREADS
  pthread_t* threads;
  pthread_mutex_init(&get_task_mutex, NULL);
  pthread_mutex_init(&write_union_mutex, NULL);
  pthread_mutex_init(&invert_mutex, NULL);
  pthread_mutex_init(&dump_mutex, NULL);
  threads = calloc(sizeof(pthread_t), num_threads);
  for (t = 0; t < num_threads; t++)  {
    work[t].thread_num = t;
    pthread_create(&threads[t],NULL, 
		   (void*(*)(void*)) parallel_do_cluster_servant, 
		   &work[t]);
  }
  for (t = 0; t < num_threads; t++) {
    pthread_join(threads[t], NULL);
  }
  free(threads);
#else
  // Just do MPI work
  while(mpi_get_task(work)) {
    //printf("Slave  %d in perform_clustering: bounds <%d,%d>\n",myid,global_i_beg, global_i_end);

    work->i_beg=global_i_beg;
    work->i_end=global_i_end;
    work->j_beg=global_j_beg;
    work->j_end=global_j_end;
    do_cluster(work);
  }
#endif
}





void show_histogram(WorkPtr work) {
  int i, j, nc=0;
  long long int  glen=0,len=0;
  int histogram[HIST_MAX];
  memset(histogram,0,HIST_MAX*sizeof(int));
  for (i=reindex_value; i<num_seqs; i++) {
    if (tree[i].cluster == i) {
      nc++; 
      len = 0;
      for (j=i; j>=0; j = tree[j].next) {
        len++;
      }
      if (len>=HIST_MAX-1) histogram[HIST_MAX-1]++;
      else histogram[len]++;
      glen += len*len;
    }
  }
  fprintf(outf,"\nNumber of clusters is %d with ave sq of len %ll\n\n",nc,glen/nc);
  fprintf(outf,"Size of Cluster    Number of Clusters\n");
  for(i=1; i<HIST_MAX; i++) 
    if (histogram[i])
      fprintf(outf,"%8d %8d\n",i,histogram[i]);
}


int is_flag(int flagvalue, int x) 
{ int fv;
  int ans;
  fv  = (seqInfo[x].flag&flagvalue);
  ans = (fv == flagvalue);
  return ans; }

//-------------------------------------------------------------

void get_clustering(char * fname, int offset) {
  // read the clustering for sequences i to j
  // in the input file the sequence number will be give as 0..j-i
  FILE * cfn;
  char tmp[4095];
  int    res,curr,the_next,last,cnum,prev,validcurr;

  cfn = fopen(fname,"r");
  if (cfn == NULL) {
    perror(fname);
    exit(1);
  }
  // Are we restoring from a checkpoint
  if (prog_opts.restore) {  // first line of file will contain
    cnum=num_seqs; // round numbers. Find the min value
    for(res=1; res<numprocs; res++) {
      fscanf(cfn,"%d\n",&the_next);
      if(the_next<cnum) cnum = the_next;
    }
    prog_opts.restore= cnum;
    //printf("Restore value is %d",prog_opts.restore);
  }
  // Read the clusters
  do {
    res = fscanf(cfn, "%d", &cnum);
    cnum = cnum+offset;
    validcurr = cnum;
    if (res != 1) break;
    prev=cnum;
    while (fscanf(cfn, "%d", &curr)==1) {
      validcurr = curr+offset;
      tree[validcurr].cluster=cnum;
      tree[prev].next = validcurr;
      prev = validcurr;
      if (IS_FLAG(FIX,validcurr)) 
	SET_FLAG(FIX,cnum);
    }
    tree[validcurr].next = -1;
    // now deal with resetting
    // find the first sequencein cluster that should NOT be reset
    // --- reset all sequences up to that point
    res = is_flag(RESET,cnum);
    for(curr=cnum; 
        (curr != -1) && (is_flag(RESET,curr)); 
	curr=the_next) {
      the_next = tree[curr].next;
      tree[curr].next=-1;
      tree[curr].rank=1;
      tree[curr].cluster=curr;
      tree[curr].last=curr;
    }
    prev=-1;
    cnum=last=curr;
    // Now find the last non-reset sequence
    for(curr=cnum; curr != -1; curr=tree[curr].next) last = curr;
    // Now go through the rest -- eithe reset or link up
    for(curr=cnum; curr != -1; curr=tree[curr].next) {
      if (IS_FLAG(RESET,curr)) {
	tree[curr].next=-1;
	tree[curr].rank=1;
	tree[curr].cluster=curr;
	tree[curr].last=curr;
      } else { // link up
	if (prev != -1) 
	  tree[prev].next = curr;
	tree[curr].cluster=cnum;
	tree[curr].last = last;
	tree[cnum].rank++;
	prev = curr;
      }
    }
    res = fscanf(cfn,"%[.]",tmp);
    assert(res==1);
  } while (1);
}




void reclustering(WorkPtr work, char * fname) {
  // do a finer clustering
  FILE * cfn;
  char tmp[4095];
  int    sizecluster;
  int    res,curr;
  int * this_cluster;
  
  this_cluster = (int *) malloc(num_seqs*sizeof(int));

  cfn = fopen(fname,"r");
  if (cfn == NULL) {
    perror(fname);
    exit(1);
  }
  do {
    sizecluster=0;
    res = fscanf(cfn, "%d", &curr);
    if (res != 1) break;
    this_cluster[sizecluster]=curr;
    sizecluster++;
    while (fscanf(cfn, "%d", &curr)==1) {
      this_cluster[sizecluster] = curr;
      sizecluster++;
    }
    //if (sizecluster>1)
    //printf("Size of cluster %d\n",sizecluster);
    res = fscanf(cfn,"%[.]",tmp);
    assert(res==1);
    if (sizecluster>1) {
      work->i_beg=0;
      work->i_end=sizecluster;
      work->j_beg=0;
      work->j_end=sizecluster;
      work->index=this_cluster;
      do_cluster(work);
    }
  } while (1);
}




//-------------------------------------------------------------


void show_performance(FILE * fout) {
  fprintf(outf,"\n\nProfiling\n");
  fprintf(outf,"Number of times num_matches called: %d\n",NUM_num_matches);
  fprintf(outf,"Number  of  times  full heuristic called: %d\n",NUM_full_heuristic);
  fprintf(outf,"Number  of  times dist fun  called: %d\n",NUM_dfn);
  fprintf(outf,"Number successful dist fn  calls: %d\n",total_matches);
  fprintf(outf,"Number possible matches (n^2/2): %d\n",num_seqs*num_seqs/2);
}





//-----------------

void show_sequence(FILE *finp, int i, int opt) {
  num_seqs = i+1;
  //read_sequences(finp,0,num_seqs);
#ifndef NOAUXINFO
  fprintf(outf,">%s %d\n", seqID[i].id, seqInfo[i].len);
#else
  fprintf(outf,">Sequence%07d %d\n",i,seqInfo[i].len);
#endif
  if (opt) pseq(i); else pseqi(i);
}


void do_compare(FILE *finp, int i, int j, int opt) {
  int samp_pos, samp_rc, num_mat_pos, num_mat_rc, posd2,negd2;
  num_seqs = i>j?i:j;
  num_seqs++;
  seqInfo[i].flag=0;
  seqInfo[j].flag=0;
  set_up_word_table(work,i);
  sample_heuristic(work,i,j,&samp_pos,&samp_rc);
  tv_heuristic(work,i,j,&num_mat_pos,&num_mat_rc);
  if (seqInfo[i].len<window_len || seqInfo[j].len<window_len) {
    posd2=negd2=-1;
  } else {
    posd2 = d2pair(work,i,j,0);
    negd2 = d2pair(work,i,j,1);
  }
  if(opt) {
    show_sequence(outf,i,0);
    show_sequence(outf,j,0);
    show_sequence(outf,j,1);
    fprintf(outf,"i%6d j%6d sp:%4d sr:%4d tp%4d tr:%4d dp:%4d dr:%4d\n",
	   i,j,samp_pos,samp_rc,num_mat_pos, num_mat_rc, posd2,negd2);
  } else {
    i = posd2<negd2 ? posd2 : negd2;
    fprintf(outf,"%d\n",i);
  }
}

void matrix_compare(FILE *finp) {
  int posd2,negd2, score;
  int i, j;

  for(i=0; i<num_seqs; i++) {
    for(j=0; j<num_seqs; j++) {
      posd2 = d2(work,i,j,0);
      negd2 = d2(work,i,j,1);
      score = (posd2<negd2)? posd2 : negd2;
      fprintf(outf,"%5d",score);
    }
    fprintf(outf,"\n");
  }
  
}



void compared2nummatches(FILE *finp, int opt) {
  // This is an undocumented feature, useful for 
  // doing statistics. The current version of
  // num_matches does not necessarily compute the
  // tne number of matches unless the heuristic threshold
  // is set to 0
  int i, j;
  int num_mat_pos, num_mat_rc, posd2,negd2,d2,h1,h2,count,sp,sr;
  int passh1=0, passh2=0, passd2=0,d2table[100],h1fh2p=0,step=1;
  
  if(opt==2) step=17;
  for(i=0;i<100;i++) d2table[i]=0;
  read_sequences(finp,0,num_seqs);
  for(i=0; i<num_seqs; i=i+1) {
    set_up_word_table(work,i);
    for(j=i+1; j<num_seqs; j=j+step) {
      sample_heuristic(work,i,j,&sp,&sr);
      tv_heuristic(work,i,j,&num_mat_pos,&num_mat_rc);
      if (seqInfo[i].len<window_len || seqInfo[j].len<window_len) {
	posd2=negd2=-1;
      } else {
	if(num_mat_pos>=0 || num_mat_rc >=0) {
	  posd2 = d2pair(work,i,j,0);
	  negd2 = d2pair(work,i,j,1);
	  d2 = MIN(posd2,negd2);
	  h1 = MAX(sp,sr);
	  h2 = MAX(num_mat_pos,num_mat_rc);
	  if (h1 > 1) passh1++;
	  if (h2 >= NM_threshold) passh2++;
	  if ((num_mat_pos>= NM_threshold)&&(sp<= 1)) h1fh2p++;
	  if ((num_mat_rc >= NM_threshold)&&(sr<= 1)) h1fh2p++;
	  if (d2 <= theta) {
	    passd2++;
	    //if (count<max_count) max_count=count;
	  }
	  d2table[d2/10]++;
	  if ( (h2<7) && (d2<50)) fprintf(outf,"H2 FAIL: %d %d : h2=%d d2=%d\n",i,j,h2,d2);
	  if ( (h1<2) &&  (d2<60)) fprintf(outf,"H1 FAIL: %d %d :h1=%d,h2=%d\n",i,j, h1, d2);
 	 
	  if (opt==2)
	    fprintf(outf,"%3d,%8d,%8d,%2d,%3d,%3d,%2d,%3d,%3d,%2d,%3d,%3d\n",i,j, 
		    count,sp,num_mat_pos,posd2,sr,num_mat_rc, negd2,
		    MAX(sp,sr), MAX(num_mat_pos,num_mat_rc),
		    MIN(posd2,negd2));
	}
      }
    }
    clear_word_table(work,i);
  } 
  fprintf(outf,"h1=%d; h2 %d; h1fh2p %d; d2 %d\n",passh1,passh2,h1fh2p,passd2);
  for(i=0; i<40; i++) {
    fprintf(outf,"%3d %10d\n",i,d2table[i]);
  }
}





void show_pairwise(FILE *finp, int i, int j) {
  // shows score between sequence i and j
  int dplus, drc;

  num_seqs = i>j?i:j;
  num_seqs++;
  read_sequences(finp,0,num_seqs);
  dplus = distpair(work,i,j,0);
  drc   = distpair(work,i,j,1);
  fprintf(outf,"%d %d\n",dplus,drc);
}


void cluster_compare(char * clusterfname) {
  // compares two clusters. The parameter is a file name which should
  // contain two clusters (one on each line). It then compares each 
  // sequence in the one cluster with the sequence in the other
  FILE * clusterf;
  int *index1, *index2, i, j;

  clusterf = fopen(clusterfname,"r");
  if (clusterf == NULL) { 
    perror(clusterfname);
    exit(1);
  }
  index1 = (int *) malloc(num_seqs*sizeof(int));
  index2 = (int *) malloc(num_seqs*sizeof(int));
  for(i=0; fscanf(clusterf,"%d",&index1[i])==1; i++);
  index1[i]=-1;
  fscanf(clusterf,".");
  for(i=0; fscanf(clusterf,"%d",&index2[i])==1; i++);
  index2[i]=-1;
  for(i=0; index1[i]!=-1; i++) {
    for(j=0; index2[j]!=-1; j++) {
      fprintf(outf,"%10d %10d: %5d %5d\n",index1[i],index2[j],
	      d2(work,index1[i],index2[j],0),d2(work,index1[i],index2[j],1));
    }
  }
  free(index1);
  free(index2);
}




void print_options() {
  fprintf(outf,"\n\nUsage: wcd -v | -u |-h \n");
  fprintf(outf,"   -v: version\n");
  fprintf(outf,"   -u: usage\n");
  fprintf(outf,"   -h: usage\n\n");
  fprintf(outf,"\n\nUsage: wcd [opts] <filename> \n");
  fprintf(outf,"   -C val, --num_seqs val: only process this many sequences\n");
  fprintf(outf,"   -c, --show_clusters: show clusters compactly\n");
  fprintf(outf,"   -d fname: use the file as a dump file\n");
  fprintf(outf,"   -F fun, --function fun: say which distance function to use");
  fprintf(outf,"   -f fname, --init_cluster: use file as initial cluster\n");
  fprintf(outf,"   -g, --histogram: show histogram\n");
  fprintf(outf,"   -G, --produce_clusters criteria@dir: produce clustering");
  fprintf(outf,"   -H val, --common_word val: set common word heuristic threshold (default 65)\n");
  fprintf(outf,"   -j fname, --constraint1 fname: set constraint file 1.\n");
  fprintf(outf,"   -J fname, --constraint2 fname: set constraint file 2.\n");
  fprintf(outf,"   -k fname, --split: give the split file name\n\n");
  fprintf(outf,"   -K val, --sample_thresh val: give sample heuristic threshold\n\n");
  fprintf(outf,"   -: val, --checkpoint: checkpoiinting file\n");
  fprintf(outf,"   -l val, --window_len: set window len (default 100)\n");
  fprintf(outf,"   -N val  : use N processors in Pthreads\n");
  fprintf(outf,"   -n, --no_rc: don't do RC check\n");
  fprintf(outf,"   -o fname , --output fname:  send output to file [stdout]");
  fprintf(outf,"   -P fname, --parameter fname : parameter file for distance function\n");
  fprintf(outf,"   -Q int, --reindex int: reindex sequence ids.\n");
  fprintf(outf,"   -R <filename> <ind1> <ind2>:  perform cluster on a range\n"); 
  fprintf(outf,"   -s, --performance: show performance stats\n");
  fprintf(outf,"   -S val, --skip val: set skip value\n");
  fprintf(outf,"   -t, --show_ext: show extended cluster table\n");
  fprintf(outf,"   -T val, --threshold val: set threshold to value\n");
  fprintf(outf,"   -w val, --word_len: set d2 word length (default 6)\n");
  fprintf(outf,"   -X: generate crude stats on clustering\n");
  fprintf(outf," \nUsage: wcd [-i|--show_seq] <index>  <filename>\n");
  fprintf(outf,"         show the sequence with the given index\n");
  fprintf(outf," \nUsage: wcd [-I|--show_rc_seq] <index>  <filename>\n");
  fprintf(outf,"         show the RC of the sequence with the given index\n");
  fprintf(outf," \nUsage: wcd [-E|-e|-p] <filename> <ind1> <ind2>\n");
  fprintf(outf,"   -E, --compare: show seqs, number common words, and d2scores\n");
  fprintf(outf,"   -e, --abbrev_compare: show min of d2scores (pos + rc)\n");
  fprintf(outf,"   -p, --pairwise: show pairwise d2 scores of all windows\n");
  fprintf(outf," \nUsage: wcd [--cluster_compare,-D] <seqfile> <indicesfile>\n");   
  fprintf(outf,"              compare two clusters\n");
  fprintf(outf," \nUsage: wcd [--merge,-m] <seqf1> <clf1> <seqf2> <clf2>\n");     
  fprintf(outf,"           merge two clusterings\n\n");  
  fprintf(outf," \nUsage: wcd [--add,-a] <seqf1> <clf1> <seqf2> \n");     
  fprintf(outf,"           add a set of sequences to a known clustering\n\n");  
  fprintf(outf," \nUsage: wcd [--recluster,-r] <seqf1> <clf1>\n");     
  fprintf(outf,"           recluster from a less stringent clustering\n\n");  

}



void chooseFun(char * dfunname) {
  if(strcmp(dfunname,"suffix") == 0) {
    dist = suffix;
    do_cluster = do_suffix_cluster;
  }
  else if(strcmp(dfunname,"d2")==0) {
    dist = d2;
    distpair= d2pair;
  } else if (strcmp(dfunname,"ed")==0) {
    word_len = 1;
    theta    = -20;
    dist  = ed;
    distpair= edpair;
  } else if ((strcmp(dfunname,"heuristic")==0)) {
    dist  = true_function;
    distpair = true_function;
  } else if (strcmp(dfunname,"word")==0) {
    
  }
}






int handleMerge(char * fname, int num_seqs) {
  int i;

  if (prog_opts.init_cluster) {
    printf("Cannot use init_cluster option with merge or add.\n");
    printf("Give clustering as file arguments.\n");
    assert(0);
  }
  i = count_seqs(fname);
  if (prog_opts.consfname2) 
    process_constraints(prog_opts.consfname2,num_seqs);
  num_seqs = num_seqs+i;
  return num_seqs;
}




void process_options(int argc, char * argv[]) {
  int opt, m;
  char outfname[255];

  while ((opt = GETOPT(argc,argv,allowed_options, long_options,0)) != -1) {
    switch (opt) {
	case '@':
	  sscanf(optarg,"%d",&alpha);
	  break;
	case '$':
	  sscanf(optarg,"%d",&beta);
	  break;
    case 'A':
      prog_opts.restore = 1;
      break;
    case 'a':
      prog_opts.doadd = 1;
      break;
    case 'c' : prog_opts.show_clust |= 1;
      break;
    case 'C' :
      sscanf(optarg, "%d", &num_seqs);
      break;
    case 'D' :
      prog_opts.clustercomp = 1;
      break;
    case 'd' :
      prog_opts.do_dump=1;
      prog_opts.dname = (char *) malloc(strlen(optarg)+12);
      sscanf(optarg,"%s",prog_opts.dname);
      break;
    case 'x' :
      prog_opts.show_comp = 41;
     break;
    case 'e' :
      prog_opts.flag = 1;
    case 'E' :
      prog_opts.show_comp = 1; 
      break;
    case 'F' :
      chooseFun(optarg);      
      break;
    case 'M':
      prog_opts.pairwise=2;
      break;
    case 'f' :
      prog_opts.init_cluster =1 ;
      prog_opts.clfname1 = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.clfname1);
      break;
    case 'G':
      prog_opts.dirname = (char *) malloc(strlen(optarg));
      strcpy(prog_opts.dirname,optarg);
      m=sscanf(optarg,"%d@%s",&prog_opts.clthresh,prog_opts.dirname);
      prog_opts.show_clust |= 8;
      break;
    case 'g' : prog_opts.show_histo=1;
      break;
    case 'h' :
      print_options();
      exit(0);
      break;
    case 'H' :
      sscanf(optarg,"%d",&NM_threshold);
      break;
    case 'i' : 
      prog_opts.flag =1;
    case 'I' :
      sscanf(optarg,"%d",&prog_opts.index);
      prog_opts.show_index = 1;
      break;
    case 'j':
      prog_opts.consfname1 = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.consfname1);
      break;
    case 'J':
      prog_opts.consfname2 = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.consfname2);
      break;
    case 'k' :
      prog_opts.split = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.split);
      break;
    case 'K' :
      sscanf(optarg, "%d", &sample_thresh);
      break;
    case 'L' :
      prog_opts.checkpoint = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.checkpoint);
      break;
    case 'l' :
      sscanf(optarg,"%d",&window_len);
      break;
    case 'm' :
      prog_opts.domerge =1;
      break;
    case 'N' :
      sscanf(optarg,"%d",&num_threads);
      break;
    case 'n' : rc_check=0;
      break;
    case 'o' :
      sscanf(optarg,"%s",outfname);
      outf = fopen(outfname,"w");
      if (outf == NULL) {
	perror(outfname);
	exit(2);
      }
      break;
    case 'p' :
      prog_opts.pairwise=1;
      break;
    case 'P' :
      prog_opts.parmfile = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.parmfile);
      prog_opts.edfile = fopen(prog_opts.parmfile,"r");
      if (prog_opts.edfile == NULL) {
	perror(prog_opts.parmfile);
	exit(1);
      }
      break;
    case 'Q' :
      sscanf(optarg,"%d",&reindex_value);
      break;
    case 'R' :
      prog_opts.range = 1;
      break;
    case 'r' :
      prog_opts.recluster = 1;
      prog_opts.clfname2 = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.clfname2);
      break;
    case 's' : prog_opts.show_perf = 1;
      break;
    case 'S' :
      sscanf(optarg,"%d",&skip);
      break;
    case 'T' :
      sscanf(optarg,"%d",&theta);
      break;
    case 't' : prog_opts.show_ext = 1;
      break;
	case 'U' :
	  sscanf(optarg,"%d",&suffix_len);
	  break;
    case 'v' :
      fprintf(outf,"\n%s\n   File version: %s\n",version,vcid);
#ifndef PTHREADS
      fprintf(outf,"Not ");
#endif
      fprintf(outf,"configured for pthreads\n");
#ifndef MPI
      fprintf(outf,"Not ");
#endif
      fprintf(outf,"configured for MPI\n");
      exit(0);
      break;
    case 'V' :
      prog_opts.show_version=1;
      break;
    case 'X' :
      clonelink=1;
      break;
    case 'Z' :
      prog_opts.statgen=2;
      break;
    case 'w' :
      sscanf(optarg,"%d",&word_len);
      break;
    case 'z' :
      prog_opts.noninterleavednlc=1;
      break;
    default: exit(-2);
    }
  }
}



void process_slave_options(int argc, char * argv[]) {
  int opt;

  while ((opt = GETOPT(argc,argv,allowed_options, long_options,0)) != -1) {
    switch (opt) {
    case 'c' : 
      break;
    case 'C' :
      sscanf(optarg, "%d", &num_seqs);
      break;
    case 'D' :
      break;
    case 'd' :
      break;
    case 'e' :
    case 'E' :
      break;
    case 'F' :
      chooseFun(optarg);      
      break;
    case 'f' :
      prog_opts.init_cluster =1 ;
      prog_opts.clfname1 = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.clfname1);
      break;
    case 'g' : 
      break;
    case 'h' :
    case 'u' : 
      break;
    case 'H' :
      sscanf(optarg,"%d",&NM_threshold);
      break;
    case 'I' : 
    case 'i' :
      break;
    case 'k' :
      prog_opts.split = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.split);
      break;
    case 'K' :
      sscanf(optarg, "%d", &sample_thresh);
      break;
    case 'l' :
      sscanf(optarg,"%d",&window_len);
      break;
    case 'm' :
      break;
    case 'N' :
      sscanf(optarg,"%d",&num_threads);
      break;
    case 'n' : rc_check=0;
      break;
    case 'p' :
      break;
    case 'P' :
      prog_opts.parmfile = (char *) malloc(strlen(optarg)+2);
      sscanf(optarg,"%s",prog_opts.parmfile);
      prog_opts.edfile = fopen(prog_opts.parmfile,"r");
      if (prog_opts.edfile == NULL) {
	perror(prog_opts.parmfile);
	exit(1);
      }
      break;
    case 'R' :
      break;
    case 'r' :
      break;
    case 's' : prog_opts.show_perf = 1;
      break;
    case 'S' :
      sscanf(optarg,"%d",&skip);
      break;
    case 'T' :
      sscanf(optarg,"%d",&theta);
      break;
    case 't' : 
      break;
    case 'v' :
      break;
    case 'V' :
      break;
    case 'X' :
      break;
    case 'w' :
      sscanf(optarg,"%d",&word_len);
      break;
    default: break;
    }
  }
}



void internalTest() {
  int i,j;

  set_up_word_table(work,0);
  set_up_word_table(work,1);
  
  for(i=0; i<num_seqs; i++) {
    //printf("%d len=%d <%s>\n",i,seqInfo[i].len,seqID[i].id);
    assert(seqInfo[i].len >= 0);
    assert(seqInfo[i].len < 30000);
    assert(tree[i].cluster == i);
    assert(tree[i].next == -1);
    assert(tree[i].last == i);
  }

  for(i=0; i<data_size; i++) {
    j = (j+data[i])%3;

  }

}

void init_dummy_sequences() {

  int i;
  for(i=0; i<reindex_value; i++) {
    seq[i]=data;
    seqInfo[i].len = 0;
    tree[i].cluster  = i;
    tree[i].last     = i;
    tree[i].next     = -1;
    tree[i].match    = -1;
    tree[i].orient   = 1;
    tree[i].rank     = 0;
    if (IGNORE_SEQ(i)) continue;
  }
}





int main(int argc, char *argv[]) {
  struct tms usage;
  FILE *finp;
  int i,j, ticks;
  int numinfirst;
  char chkfile[255];

  i=0;
  dump_file=NULL;

  do_cluster=do_pairwise_cluster;
  srandom(563573);
  bzero(&prog_opts,sizeof(ProgOptionsType));
  outf=stdout;
  // set default distance function
  dist = d2;
  distpair= d2pair;
#ifdef MPI
  MPI_Init(&argc, &argv);
  MPI_Errhandler_set(MPI_COMM_WORLD, MPI_ERRORS_RETURN);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);
#endif


  if(myid==0) { // Master
    process_options(argc, argv);
  } else {
    process_slave_options(argc, argv);
  }

  if (prog_opts.show_version || (argc==1)) {
      if (myid==0) printf("Version \n%s\n",version);
#ifdef MPI      
      MPI_Finalize();
#endif
      exit(0);
    }


  // Allocate space for the RC table for big words
  rc_big = calloc(BIG_WORD_TSIZE, sizeof(SeqElt));

  // work is an array of work blocks. If non-parallel, there'll only
  // be one. work[0] acts a template

  work = (WorkPtr) calloc(num_threads,sizeof(WorkBlock));
  work->filename = argv[optind];
  work->index    = NULL;


  if(prog_opts.do_dump) dump_file = fopen(prog_opts.dname,"w");

#ifdef MPI
  if (numprocs > 1)  
    if (myid>0) {  // slaves
      if (prog_opts.split) {
	MPI_Finalize();
	return 0;
      }
      handleMPISlaveSetup(&num_seqs);
      initialise(work, prog_opts.edfile);
      internalTest();

      perform_clustering(work);
      transmitMPISlaveResponse(work);
      if (prog_opts.show_perf)     show_performance(outf);
      MPI_Finalize();
      exit(0);
    }   
#else
  if (numprocs > 1) {
    printf("This version of wcd is not compiled with MPI\n");
    printf("You cannot run it with a multiple processes\n");
    printf("Either only run it with one process or do a \n");
    printf("  ./configure --enable-mpi\n");
    printf("  make clean\n");
    printf("  make \n");
    exit(5);
  }
#endif

  // work out number of sequences
  // if the user has specified a value for num_seqs then
  // use that, else use the number of sequences in the file
  num_seqs = count_seqs(argv[optind], &data_size)+reindex_value;

  seq = (SeqPtr *) calloc(num_seqs,sizeof(SeqPtr));
  seqInfo     = (SeqInfoPtr) calloc(num_seqs,sizeof(SeqInfoStruct));
  tree= (UnionFindPtr) calloc(num_seqs,sizeof(UnionFindStruct));
  data= (SeqPtr)  calloc(data_size,sizeof(SeqElt));
  init_dummy_sequences();
#ifndef AUXINFO
  seqID = (SeqIDPtr) calloc(num_seqs,sizeof(SeqIDStruct));
#endif
  if (seq == NULL) {
    perror("SeqStruct allocation");
    exit(50);
  }
  numinfirst = global_i_end = num_seqs;
  global_j_beg = 0;
  // if merging, need to check the other file too
  if (prog_opts.domerge || prog_opts.doadd ) {
    global_j_beg = global_i_end;
    num_seqs = handleMerge(argv[optind+2], num_seqs);
    if (prog_opts.doadd) global_i_end = num_seqs; 
  }

  initialise(work, prog_opts.edfile);
  if (data == NULL) {
    sprintf(chkfile,"Main data store (%d bytes)",data_size);
    perror(chkfile);
    exit(51);
  }
  for(i=0; i<num_seqs; i++) seqInfo[i].flag=0;
  // reopen sequence file for reading
  finp = fopen(argv[optind],"r");
  if (finp == NULL)  {
    perror(argv[optind]);
    exit(51);
  }
  // Some messy stuff to hande auxiliary options
  // Skip to next comment on first reading
  if (prog_opts.pairwise==1) {
    sscanf(argv[optind+1], "%d", &i);
    sscanf(argv[optind+2], "%d", &j);
    show_pairwise(finp,i,j);
    return 0;
  }
  if (prog_opts.statgen) {
    compared2nummatches(finp,prog_opts.statgen);
    return 0;
  }
  if (prog_opts.range) {
    sscanf(argv[optind+1], "%d", &global_i_beg);
    sscanf(argv[optind+2], "%d", &global_i_end);
  }     
  if (prog_opts.show_comp==41) {
    char * fname; fname = malloc(255);
    sscanf(argv[optind+1], "%s", fname);
    read_sequences(finp,reindex_value,num_seqs); 
    checkfile = fopen(fname,"r");
    sscanf(argv[optind+2], "%d", &j);
    while (fscanf(checkfile,"%d", &i) != -1) {
    	  do_compare(finp,i,j,1);
}
    return 0;
  }

  if (prog_opts.show_comp) {
    sscanf(argv[optind+1], "%d", &i);
    sscanf(argv[optind+2], "%d", &j);
    //printf("Comparing %d and %d of %d flag %d\n",i,j,num_seqs,prog_opts.flag);
    read_sequences(finp,reindex_value,num_seqs); 
    do_compare(finp,i,j,prog_opts.flag);
    return 0;
  }
  if (prog_opts.show_index) {
    show_sequence(finp, prog_opts.index,prog_opts.flag);
    return 0;
  }
  // Now read in the sequences
  if (do_cluster == do_pairwise_cluster||do_cluster==do_MPImaster_cluster||do_cluster == do_suffix_cluster) 
    read_sequences(finp,reindex_value,numinfirst);
  else
    init_sequences(finp,reindex_value,numinfirst);
  fclose(finp);
  //printf("%d Allocated %d, start=%d, last=%d\n",num_seqs,data_size,data,seq[num_seqs-1].seq);

  if (prog_opts.split) {
    process_split(prog_opts.clfname1, prog_opts.split);
#ifdef MPI
    MPI_Finalize();
#endif
    return 0;
  }
  if (prog_opts.consfname1) process_constraints(prog_opts.consfname1,0);
  if (prog_opts.clustercomp) {
    cluster_compare(argv[optind+1]);
    return 0;
  }
  // If merging or adding need to open the second sequence file
  if (prog_opts.domerge || prog_opts.doadd) {
    finp = fopen(argv[optind+2], "r");
    if (finp == NULL)  {
      perror(argv[optind]);
      exit(1);
    }
    if (do_cluster == do_pairwise_cluster)
      read_sequences(finp,numinfirst+reindex_value,num_seqs);
    else
      init_sequences(finp,numinfirst+reindex_value,num_seqs);
    get_clustering(argv[optind+1],0);
    if (prog_opts.domerge) get_clustering(argv[optind+3],numinfirst);
  }
  if (prog_opts.init_cluster) get_clustering(prog_opts.clfname1, 0);
  if (prog_opts.recluster)
    reclustering(work,prog_opts.clfname2);
  else {
    // This really assumes there is only one thread for suffix
    if (prog_opts.pairwise==2) {
      matrix_compare(finp);
      return 0;
    }
    work->workflag = prog_opts.noninterleavednlc;//kludge for suffixarray
    global_j_end = num_seqs;
    perform_clustering(work);
#ifdef MPI
    if (myid>0) transmitMPISlaveResponse(work);
#endif
  }
  if (prog_opts.show_ext)      show_EXT(outf);
  if (prog_opts.show_histo)    show_histogram(work);
  if (prog_opts.show_clust&1) show_clusters(outf); 
  if (prog_opts.show_clust&8) 
    produce_clusters(prog_opts.clthresh,prog_opts.dirname);
  if (prog_opts.show_perf)     show_performance(outf);
  if (prog_opts.do_dump) {
    strcpy(chkfile,prog_opts.dname);
    strcat(chkfile,"-FIN");
    fclose(dump_file);
    dump_file = fopen(chkfile,"w");
    times(&usage);
    ticks = sysconf(_SC_CLK_TCK);
    fprintf(dump_file,"Completed %ld %ld", usage.tms_utime/ticks, usage.tms_stime*1000/ticks);
    fclose(dump_file);
  }
  if (prog_opts.show_version) fprintf(outf,"\n%s\n",version);
  fclose(outf);
#ifdef MPI
  MPI_Finalize();
#endif
  exit(0);
}

