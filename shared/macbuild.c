#include <stdlib.h>
#include <stdio.h>

#include <mss.h>

#define M_DEST_STEREO   1
#define M_SRC_16        2
#define M_FILTER        4
#define M_SRC_STEREO    8
#define M_VOL_SCALING   16
#define M_RESAMPLE      32
#define M_ORDER         64
#define M_UP_FILTER     128

FILE* file;

static char* Describe_operation( int op )
{
  static char output[256];
  sprintf( output,
           "%s%s%s%s%s",
           ( op & M_DEST_STEREO ) ? "_DestStereo" : "_DestMono",
           ( op & M_SRC_STEREO  ) ? ( ( op & M_ORDER ) ? "_SrcFlipped" : "_SrcStereo" ) : "_SrcMono",
           ( op & M_SRC_16      ) ? "_Src16" : "_Src8",
           ( op & M_VOL_SCALING ) ? "_Volume" : "_NoVolume",
           ( op & M_RESAMPLE    ) ? ( ( op & M_FILTER ) ? ( ( op & M_UP_FILTER ) ? "_UpFiltered" : "_DownFiltered" ) : "_Resample" ) : "_NoResample" );

  return( output );
}

static void Load_dest( int op )
{
  fprintf( file,
           "\n  // Load the first dest value\n"
           "  lwz    dest_l, 0(dest)\n" );

  if ( op & M_DEST_STEREO )
  {
    fprintf( file,
             "  lwz    dest_r, 4(dest)\n" );
  }

}

static void Start_routine( int op )
{
  fprintf( file,
           "static asm void AILCALL Merge%s( // %i\n"
	   "  register S32  FAR * FAR * dest,\n"      //r3
           "  register S32  FAR * dest_end,\n"        //r4
           "  register void const FAR * FAR * src,\n" //r5
           "  register U32  FAR * src_fract,\n"       //r6
           "  register void FAR * src_end,\n"         //r7
           "  register S32  playback_ratio,\n"        //r8
           "  register S32  scale_left,\n"            //r9
           "  register S32  scale_right,\n"           //r10
           "  S32  FAR * cur_l_ptr,\n"                //stack
           "  S32  FAR * cur_r_ptr\n"                 //stack
           ")\n"
           "{\n"
	   , Describe_operation( op ), op);

  fprintf( file,
           "  // no automatic stack frame\n"
           "  nofralloc\n" );


  if ( op & M_UP_FILTER )
  {
    fprintf( file,
             "\n  // there is no stack setup, because it has already been performed\n"
             "  //   by the down filter version of this function\n" );

    fprintf( file,
             "\n  // save the upper variables\n"
             "  stw    cur_l2, save_cur_l2(sp)\n" );

    if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "  stw    cur_r2, save_cur_r2(sp)\n" );
    }

    fprintf( file,
             "\n  // setup the resample stuff\n"
               "  slwi   src_fract, src_fract, 16\n"
               "  slwi   playback_ratio, playback_ratio, 16\n" );
  }
  else
  {
    fprintf( file,
             "  stwu   sp, - MIXER_STACK_SIZE( sp )\n"
             "\n  // save the incoming pointers\n"
             "  stw    dest, dest_ptr(sp)\n"
             "  stw    src, src_ptr(sp)\n" );

    if ( op & M_RESAMPLE )
    {
      fprintf( file,
               "  stw    src_fract, src_fract_ptr(sp)\n" );
    }

    fprintf( file,
             "\n  // save the upper registers\n" );

    if ( ( op & M_SRC_STEREO ) || ( ( op & M_DEST_STEREO ) && ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "  stw    sample_r, save_sample_r(sp)\n" );
    }

    if ( op & M_FILTER )
    {
      fprintf( file,
               "  stw    divider_l, save_divider_l(sp)\n"
               "  stw    cur_l1, save_cur_l1(sp)\n" );

      if ( ( ( op & M_SRC_STEREO ) && ( op & M_DEST_STEREO ) ) || ( ( ( op & M_DEST_STEREO ) || ( op & M_SRC_STEREO ) ) && ( op & M_VOL_SCALING ) ) )
      {
        fprintf( file,
                 "  stw    divider_r, save_divider_r(sp)\n" );
      }

      if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
      {
        fprintf( file,
                 "  stw    cur_r1, save_cur_r1(sp)\n" );
      }

    }

    if ( op & M_DEST_STEREO )
    {
      fprintf( file,
               "  stw    dest_r, save_dest_r(sp)\n" );
    }

    fprintf( file,
             "\n  // load the initial register values\n" );

    if ( op & M_FILTER )
    {
      fprintf( file,
               "  lwz    cur_l1, MIXER_STACK_SIZE + cur_l_ptr(sp)\n" );

      if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
      {
        fprintf( file,
                 "  lwz    cur_r1, MIXER_STACK_SIZE + cur_r_ptr(sp)\n" );
      }
    }

    fprintf( file,
             "  lwz    dest, 0(dest)\n"
             "  lwz    src, 0(src)\n" );

    if ( op & M_RESAMPLE )
    {
      fprintf( file,
               "  lwz    src_fract, 0(src_fract)\n" );
    }


    if ( op & M_FILTER )
    {
      fprintf( file,
               "  lwz    cur_l1, 0(cur_l1)\n" );

      if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
      {
        fprintf( file,
                 "  lwz    cur_r1, 0(cur_r1)\n" );
      }
    }

  }

  if ( ! ( op & M_FILTER ) || ( op & M_UP_FILTER ) )
  {
    if ( ( ! ( op & M_SRC_16 ) ) && ( op & M_VOL_SCALING ) )
    {

      fprintf( file,
               "\n  // incorporate 8 to 16 upscale into volume\n"
               "  slwi   scale_left, scale_left, 8\n" );

      if ( ( op & M_DEST_STEREO ) || ( op & M_SRC_STEREO ) )
      {
        fprintf( file,
                 "  slwi   scale_right, scale_right, 8\n" );
      }
    }
  }
  else
  {
    fprintf( file,
             "\n  // check to see if we have to call the upsampling version\n"
             "  andis. sample_l, playback_ratio, 0xffff\n"
             "  beq    Merge%s\n", Describe_operation( op | M_UP_FILTER ) );

    if ( ( op & M_FILTER ) &&
         ( ! ( op & M_UP_FILTER ) ) &&
         (
           ( ( op & M_SRC_STEREO )  &&
             (
               ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING )
             )
           )
           ||
           ( ( op & M_DEST_STEREO ) && ( op & M_VOL_SCALING ) )
         )
       )
    {
      fprintf( file,
               "\n  // build average dividers\n");

      if ( op & M_VOL_SCALING )
      {
        if ( ! ( op & M_SRC_16 ) )
        {
          fprintf( file,
                   "  lis    divider_l, 16 // 0x100000000/2048/32*16\n" );
        }
        else
        {
          fprintf( file,
                   "  lis    divider_l, 1  // 0x100000000/2048/32\n" );
        }

        fprintf( file,
                 "  mullw  divider_l, divider_l, scale_left\n" );
      }
      else
      {
        if ( ! ( op & M_SRC_16 ) )
        {
          fprintf( file,
                   "  lis    divider_l, 0x8000  // 0x100000000/32*16\n" );
        }
        else
        {
          fprintf( file,
                   "  lis    divider_l, 0x800  // 0x100000000/32\n" );
        }
      }

      fprintf( file,
               "  divwu  divider_l, divider_l, playback_ratio\n" );

      if ( ! ( op & M_SRC_16 ) )
      {
        fprintf( file,
                 "  slwi   divider_l, divider_l, 4  // *16\n" );
      }

      if ( op & M_VOL_SCALING )
      {
        if ( ! ( op & M_SRC_16 ) )
        {
          fprintf( file,
                   "  lis    divider_r, 16 // 0x100000000/2048/32*16\n" );
        }
        else
        {
          fprintf( file,
                   "  lis    divider_r, 1  // 0x100000000/2048/32\n" );
        }

        fprintf( file,
                 "  mullw  divider_r, divider_r, scale_right\n" );
      }
      else
      {
        if ( ! ( op & M_SRC_16 ) )
        {
          fprintf( file,
                   "  lis    divider_r, 0x8000  // 0x100000000/32*16\n" );
        }
        else
        {
          fprintf( file,
                   "  lis    divider_r, 0x800  // 0x100000000/32\n" );
        }
      }

      fprintf( file,
               "  divwu  divider_r, divider_r, playback_ratio\n" );

      if ( ! ( op & M_SRC_16 ) )
      {
        fprintf( file,
                 "  slwi   divider_r, divider_r, 4  // *16\n" );
      }

    }
    else
    {
      fprintf( file,
               "\n  // build average dividers\n" );

      if ( op & M_VOL_SCALING )
      {
        if ( ! ( op & M_SRC_16 ) )
        {
          fprintf( file,
                   "  lis    divider_l, 16 // 0x100000000/2048/32*16\n" );
        }
        else
        {
          fprintf( file,
                   "  lis    divider_l, 1  // 0x100000000/2048/32\n" );
        }

        fprintf( file,
                 "  mullw  divider_l, divider_l, scale_left\n" );
      }
      else
      {
        if ( ! ( op & M_SRC_16 ) )
        {
          fprintf( file,
                   "  lis    divider_l, 0x8000 // 0x100000000/32*16\n" );
        }
        else
        {
          fprintf( file,
                   "  lis    divider_l, 0x800  // 0x100000000/32\n" );
        }
      }

      fprintf( file,
               "  divwu  divider_l, divider_l, playback_ratio\n" );
    }

    if ( ! ( op & M_SRC_16 ) )
    {
      fprintf( file,
               "  slwi   divider_l, divider_l, 4  // *16\n" );
    }

    fprintf( file,
             "\n  // handle start up loop management\n"
             "  srwi   r0, src_fract, 30\n"
             "  clrlwi src_fract, src_fract, 2\n"
             "  cmplwi r0, 2\n"
             "  cmplwi cr2, r0, 1\n"
             "\n  // load dest in case we jump right into the loop" );

    Load_dest( op );

    fprintf( file,
             "  bge   whole_continue\n"
             "  beq   cr2, last_continue\n" );
  }

}

static void Load_sample( int op )
{
  fprintf( file,
           "\n  // Load sample data\n" );

  if ( op & M_SRC_STEREO )
  {
    if ( op & M_SRC_16 )
    {
      if ( op & M_ORDER )
      {
        fprintf( file,
                 "  lwbrx  sample_l, 0, src\n"
                 "  extsh  sample_r, sample_l\n"
                 "  srawi  sample_l, sample_l, 16\n" );
      }
      else
      {
        fprintf( file,
                 "  lwbrx  sample_r, 0, src\n"
                 "  extsh  sample_l, sample_r\n"
                 "  srawi  sample_r, sample_r, 16\n" );
      }
    }
    else
    {
      if ( op & M_ORDER )
      {
        fprintf( file,
                 "  lhz    sample_l, 0(src)\n"
                 "  srwi   sample_r, sample_l, 8\n"
                 "  andi.  sample_l, sample_l, 0xff\n"
                 "  subi   sample_r, sample_r, 0x80\n"
                 "  subi   sample_l, sample_l, 0x80\n" );
      }
      else
      {
        fprintf( file,
                 "  lhz    sample_r, 0(src)\n"
                 "  srwi   sample_l, sample_r, 8\n"
                 "  andi.  sample_r, sample_r, 0xff\n"
                 "  subi   sample_l, sample_l, 0x80\n"
                 "  subi   sample_r, sample_r, 0x80\n" );
      }

    }
  }
  else
  {
    if ( op & M_SRC_16 )
    {
      fprintf( file,
               "  lhbrx  sample_l, 0, src\n"
               "  extsh  sample_l, sample_l\n" );
    }
    else
    {
      fprintf( file,
               "  lbz    sample_l, 0(src)\n"
               "  subi   sample_l, sample_l, 0x80\n" );
    }
  }

  // if dest mono and src stereo, then tie the channels together
  if ( ( ! ( op & M_DEST_STEREO ) ) && ( op & M_SRC_STEREO ) && ( ! ( op & M_VOL_SCALING ) ) )
  {
    fprintf( file,
             "\n  // Merge left and right channels for mono dest\n"
             "  add    sample_l, sample_l, sample_r\n" );

    op &= ~M_SRC_STEREO;
  }

  if ( op & M_UP_FILTER )
  {
    if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "\n  // rotate filter values\n"
               "  mr     cur_l2, cur_l1\n"
               "  mr     cur_l1, sample_l\n"
               "  mr     cur_r2, cur_r1\n"
               "  mr     cur_r1, sample_r\n" );
    }
    else
    {
      fprintf( file,
               "\n  // rotate filter values\n"
               "  mr     cur_l2, cur_l1\n"
               "  mr     cur_l1, sample_l\n" );
    }
  }
}


static void Apply_volume( int op )
{
  // filtering does the volume in the filter, so don't do it again
  if ( ! ( op & M_FILTER ) )
  {

    if ( ( ! ( op & M_DEST_STEREO ) ) && ( op & M_SRC_STEREO ) && ( ! ( op & M_VOL_SCALING ) ) )
    {
      op &= ~M_SRC_STEREO;
    }

    if ( ( op & M_DEST_STEREO ) && ( ! ( op & M_SRC_STEREO ) ) && ( op & M_VOL_SCALING ) )
    {
      op |= M_SRC_STEREO;

      fprintf( file,
               "\n  // Apply volume into each sample\n"
                 "  mullw  sample_r, sample_l, scale_right\n"
                 "  mullw  sample_l, sample_l, scale_left\n");
    }
    else
    {

      fprintf( file,
               "\n  // Apply volume\n");

      if ( op & M_VOL_SCALING )
      {
        fprintf( file,
                 "  mullw  sample_l, sample_l, scale_left\n");

        if ( op & M_SRC_STEREO )
        {
          fprintf( file,
                   "  mullw  sample_r, sample_r, scale_right\n");
        }
      }
      else
      {
        fprintf( file,
                 "  slwi   sample_l, sample_l, %i\n", ( op & M_SRC_16 ) ? 11 : 19 );

        if ( op & M_SRC_STEREO )
        {
          fprintf( file,
                   "  slwi   sample_r, sample_r, %i\n", ( op & M_SRC_16 ) ? 11 : 19 );
        }
      }
    }

    // if dest mono and src stereo, then tie the channels together
    if ( ( ! ( op & M_DEST_STEREO ) ) && ( op & M_SRC_STEREO ) )
    {
      fprintf( file,
               "\n  // Merge left and right channels for mono dest\n"
               "  add    sample_l, sample_l, sample_r\n" );
    }
  }

}


static void Start_loop( int op )
{
  int size = ( ( op & M_SRC_16 ) ? 2 : 1 ) * ( ( op & M_SRC_STEREO ) ? 2 : 1 );

  fprintf( file,
           "\n  // Merge sample data loop\n"
           "  merge_loop:\n" );

  Load_dest( op );

 }


static void Filter_with_volume( int op )
{
  if ( op & M_UP_FILTER )
  {
    fprintf( file,
             "\n  // Upsample the data points\n" );

    if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "  srwi   r0, src_fract, 16\n"
               "  sub    sample_l, cur_l1, cur_l2\n"
               "  sub    sample_r, cur_r1, cur_r2\n"
               "  mullw  sample_l, sample_l, r0\n"
               "  mullw  sample_r, sample_r, r0\n"
               "  srawi  sample_l, sample_l, 16\n"
               "  srawi  sample_r, sample_r, 16\n"
               "  add    sample_l, sample_l, cur_l2\n"
               "  add    sample_r, sample_r, cur_r2\n" );
    }
    else
    {
      fprintf( file,
               "  srwi   r0, src_fract, 16\n"
               "  sub    sample_l, cur_l1, cur_l2\n"
               "  mullw  sample_l, sample_l, r0\n"
               "  srawi  sample_l, sample_l, 16\n"
               "  add    sample_l, sample_l, cur_l2\n" );
    }

    Apply_volume( op & ~ M_FILTER );

  }
  else if ( op & M_FILTER )
  {
    int size = ( ( op & M_SRC_16 ) ? 2 : 1 ) * ( ( op & M_SRC_STEREO ) ? 2 : 1 );

    if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "\n  // weight the initial sample\n"
               "  mullw  sample_l, cur_l1, src_fract\n"
               "  mullw  sample_r, cur_r1, src_fract\n"
               "  add    src_fract, src_fract, playback_ratio\n"
               "  srawi  sample_l, sample_l, 16\n"
               "  srawi  sample_r, sample_r, 16\n"
               "  sub    cur_l1, cur_l1, sample_l\n"
               "  sub    cur_r1, cur_r1, sample_r\n"
               "  andis. r0, src_fract, 0xfffe\n"
               "  beq-    skip_loop\n"
               "\n  // loop to load all of the full sample points\n"
               "  whole_loop:\n"
               "  cmpw   src, src_end\n"
               "  bge-   src_whole_exit\n"
               "  whole_continue:\n" );

      Load_sample( op );

      fprintf( file,
               "  add    cur_l1, cur_l1, sample_l\n"
               "  add    cur_r1, cur_r1, sample_r\n"
               "  subis  src_fract, src_fract, 1\n" );

      fprintf( file,
               "  addi   src, src, %i\n",
               size );

      fprintf( file,
               "  andis. r0, src_fract, 0xfffe\n"
               "  bne-   whole_loop\n"
               "\n  skip_loop:\n"
               "  clrlwi src_fract, src_fract, 16\n"
               "  cmpw   src, src_end\n"
               "  bge-   src_last_exit\n"
               "  last_continue:\n" );

      Load_sample( op );

      fprintf( file,
               "  addi   src, src, %i\n",
               size );

      fprintf( file,
               "\n  // weight the final sample\n"
               "  mullw  r0, sample_l, src_fract\n"
               "  srawi  r0, r0, 16\n"
               "  add    r0, cur_l1, r0\n"
               "  mr     cur_l1, sample_l\n"
               "  #define temp1 sample_l\n"
               "  mullw  temp1, sample_r, src_fract\n"
               "  srawi  temp1, temp1, 16\n"
               "  add    temp1, cur_r1, temp1\n"
               "  mr     cur_r1, sample_r\n"
               "  mullw  sample_r, temp1, divider_r\n"
               "  mullw  sample_l, r0, divider_l\n"
               "  #undef temp1\n" );

      // if dest mono and src stereo, then tie the channels together
      if ( ( ! ( op & M_DEST_STEREO ) ) && ( op & M_SRC_STEREO ) )
      {
        fprintf( file,
                 "\n  // Merge left and right channels for mono dest\n"
                 "  add    sample_l, sample_l, sample_r\n" );
      }
    }
    else
    {
      fprintf( file,
               "\n  // weight the initial sample\n"
               "  mullw  sample_l, cur_l1, src_fract\n"
               "  add    src_fract, src_fract, playback_ratio\n"
               "  srawi  sample_l, sample_l, 16\n"
               "  sub    cur_l1, cur_l1, sample_l\n"
               "  andis. r0, src_fract, 0xfffe\n"
               "  beq-   skip_loop\n"
               "\n  // loop to load all of the full sample points\n"
               "  whole_loop:\n"
               "  cmpw   src, src_end\n"
               "  bge-   src_whole_exit\n"
               "  whole_continue:\n" );

      Load_sample( op );

      fprintf( file,
               "  add    cur_l1, cur_l1, sample_l\n"
               "  subis  src_fract, src_fract, 1\n" );

      fprintf( file,
               "  addi   src, src, %i\n",
               size );

      fprintf( file,
               "  andis. r0, src_fract, 0xfffe\n"
               "  bne-    whole_loop\n"
               "\n  skip_loop:\n"
               "  clrlwi src_fract, src_fract, 16\n"
               "  last_continue:\n"
               "  cmpw   src, src_end\n"
               "  bge-   src_last_exit\n" );

      Load_sample( op );

      fprintf( file,
               "  addi   src, src, %i\n",
               size );

      fprintf( file,
               "\n  // weight the final sample\n"
               "  mullw  r0, sample_l, src_fract\n"
               "  srawi  r0, r0, 16\n"
               "  add    r0, cur_l1, r0\n"
               "  mr     cur_l1, sample_l\n" );

      if ( ( op & M_DEST_STEREO ) && ( ! ( op & M_SRC_STEREO ) ) && ( op & M_VOL_SCALING ) )
      {
        fprintf( file,
                 "\n  // Duplicate the left channel into the right\n"
                 "  mullw  sample_r, r0, divider_r\n"
                 "  mullw  sample_l, r0, divider_l\n" );
      }
      else
      {
        fprintf( file,
                 "  mullw  sample_l, r0, divider_l\n" );
      }
    }

  }
}

static void Low_merge_sample( int op )
{
  fprintf( file,
           "\n  // Merge sample data into output buffer\n"
           "  add    dest_l, dest_l, sample_l\n" );

  if ( op & M_DEST_STEREO )
  {
    if ( ( op & M_SRC_STEREO ) || ( ( op & M_DEST_STEREO ) && ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "  add    dest_r, dest_r, sample_r\n"
               "  stw    dest_l, 0(dest)\n"
               "  stw    dest_r, 4(dest)\n" );
    }
    else
    {
      fprintf( file,
               "  add    dest_r, dest_r, sample_l\n"
               "  stw    dest_l, 0(dest)\n"
               "  stw    dest_r, 4(dest)\n" );
    }
    fprintf( file,
             "  addi   dest, dest, 8\n" );
  }
  else
  {
    fprintf( file,
             "  stw    dest_l, 0(dest)\n"
             "  addi   dest, dest, 4\n" );
  }
}

static void Merge_sample( int op )
{
  Low_merge_sample( op );

  fprintf( file,
           "  cmpw   dest, dest_end\n" );

  if ( ( op & M_FILTER) && ( ! ( op & M_UP_FILTER ) ) )
  {
    fprintf( file,
             "  blt+   merge_loop\n" );
  }
  else
  {
    fprintf( file,
             "  bge-   dest_end_exit\n" );
  }
}


static void Add_source( int op )
{
  int shift = ( ( op & M_SRC_16 ) ? 1 : 0 ) + ( ( op & M_SRC_STEREO ) ? 1 : 0 );
  int size = 1 << shift;

  if ( op & M_FILTER )
  {
    if ( op & M_UP_FILTER )
    {
      fprintf( file,
               "\n  // Add to accumulator and advance the source correctly\n"
               "  addc   src_fract, src_fract, playback_ratio\n"
               "  subfe. r0, r0, r0\n"
               "  blt    merge_loop\n" );

      goto move_source;

    }
  }
  else
  {
    if ( op & M_RESAMPLE )
    {
      fprintf( file,
               "\n  // Add to accumulator and advance the source correctly\n"
               "  add    src_fract, src_fract, playback_ratio\n"
               "  rlwinm r0, src_fract, (32 - 16 + %i), ( 16 - %i ), ( 31 - %i )\n"
               "  clrlwi src_fract, src_fract, 16\n"
               "  add    src, src, r0\n", shift, shift, shift );
    }
    else
    {
     move_source:

      fprintf( file,
               "\n  // Move the source pointer\n");

      fprintf( file,
               "  addi   src, src, %i\n",
               size );
    }

    fprintf( file,
             "  cmpw   src, src_end\n"
             "  bge-   src_end_exit\n" );
  }

}

static void End_loop( int op )
{
  int shift = ( ( op & M_SRC_16 ) ? 1 : 0 ) + ( ( op & M_SRC_STEREO ) ? 1 : 0 );
  int size = 1 << shift;

  if ( ( ! ( op & M_FILTER ) ) || ( op & M_UP_FILTER ) )
  {
    fprintf( file,
             "\n  // End loop\n"
             "  b      merge_loop\n" );
  }

  fprintf( file,
           "\n  // Jump out point if end of dest is reached\n"
           "  dest_end_exit:\n" );

  if ( op & M_RESAMPLE )
  {
    if ( op & M_UP_FILTER )
    {
      fprintf( file,
               "  addi   src, src, %i\n",
               size );

      fprintf( file,
               "  addc   src_fract, src_fract, playback_ratio\n"
               "  subfe. r0, r0, r0\n"
               "  bge    skip_filter_adjust\n" );

      if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
      {
        fprintf( file,
                 "\n  // rotate filter values\n"
                 "  mr     cur_l1, cur_l2\n"
                 "  mr     cur_r1, cur_r2\n" );
      }
      else
      {
        fprintf( file,
                 "\n  // rotate filter values\n"
                 "  mr     cur_l1, cur_l2\n" );
      }

      fprintf( file,
               "\n  // un-increment the source to skip the early source adjustment\n" );

      fprintf( file,
               "  subi   src, src, %i\n",
               size );

      fprintf( file,
               "\n  skip_filter_adjust:\n" );
    }
    else
    {
      if ( op & M_FILTER )
      {
        fprintf( file,
                 "  b      src_save_value\n"
                 "\n  // jump out when src is exceed, but save our current loop position\n"
                 "  src_whole_exit:\n"
                 "  oris   src_fract, src_fract, 0x8000\n"
                 "  src_last_exit:\n"
                 "  oris   src_fract, src_fract, 0x4000\n"
                 "  src_save_value:\n" );
      }
      else
      {
        fprintf( file,
                 "\n  // Add to accumulator and advance the source correctly\n"
                 "  add    src_fract, src_fract, playback_ratio\n"
                 "  rlwinm r0, src_fract, (32 - 16 + %i), ( 16 - %i ), ( 31 - %i )\n"
                 "  clrlwi src_fract, src_fract, 16\n"
                 "  add    src, src, r0\n", shift, shift, shift );
      }
    }
  }
  else
  {
    fprintf( file,
             "  addi   src, src, %i\n",
             size );
  }

  if ( ( ! ( op & M_FILTER ) ) || ( op & M_UP_FILTER ) )
  {
    fprintf( file,
             "\n  // Jump out point if end of src is reached\n"
             "  src_end_exit:\n" );
  }

}

static void End_routine( int op )
{
  if ( op & M_UP_FILTER )
  {
    fprintf( file,
             "\n  // adjust fractional\n"
             "  srwi   src_fract, src_fract, 16\n" );
  }

  fprintf( file,
           "\n  // Save end results (use sample_l as a temporary) \n"
           "  lwz    sample_l, dest_ptr(sp)\n"
           "  stw    dest, 0(sample_l)\n"
           "  lwz    sample_l, src_ptr(sp)\n"
           "  stw    src, 0(sample_l)\n" );

  if ( op & M_RESAMPLE )
  {
    fprintf( file,
             "  lwz    sample_l, src_fract_ptr(sp)\n"
             "  stw    src_fract, 0(sample_l)\n" );
  }


  if ( op & M_FILTER )
  {
    fprintf( file,
             "  lwz    sample_l, MIXER_STACK_SIZE + cur_l_ptr(sp)\n"
             "  stw    cur_l1, 0(sample_l)\n" );

    if ( ( op & M_SRC_STEREO ) || ( ( op & M_DEST_STEREO ) && ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "  lwz    sample_l, MIXER_STACK_SIZE + cur_r_ptr(sp)\n"
               "  stw    cur_r1, 0(sample_l)\n" );
    }
  }

  fprintf( file,
           "\n  // restore upper variables\n" );

  if ( op & M_UP_FILTER )
  {
    fprintf( file,
             "  lwz    cur_l2, save_cur_l2(sp)\n" );

    if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "  lwz    cur_r2, save_cur_r2(sp)\n" );
    }

  }

  if ( ( op & M_SRC_STEREO ) || ( ( op & M_DEST_STEREO ) && ( op & M_VOL_SCALING ) ) )
  {
    fprintf( file,
             "  lwz    sample_r, save_sample_r(sp)\n" );
  }

  if ( op & M_FILTER )
  {
    if ( ! ( op & M_UP_FILTER ) )
    {

      fprintf( file,
               "  lwz    divider_l, save_divider_l(sp)\n" );

      if ( ( ( op & M_SRC_STEREO ) && ( op & M_DEST_STEREO ) ) || ( ( ( op & M_DEST_STEREO ) || ( op & M_SRC_STEREO ) ) && ( op & M_VOL_SCALING ) ) )
      {
        fprintf( file,
                 "  lwz    divider_r, save_divider_r(sp)\n" );
      }

    }

    fprintf( file,
             "  lwz    cur_l1, save_cur_l1(sp)\n" );

    if ( ( op & M_SRC_STEREO ) && ( ( op & M_DEST_STEREO ) || ( op & M_VOL_SCALING ) ) )
    {
      fprintf( file,
               "  lwz    cur_r1, save_cur_r1(sp)\n" );
    }

  }

  if ( op & M_DEST_STEREO )
  {
    fprintf( file,
             "  lwz    dest_r, save_dest_r(sp)\n" );
  }

  fprintf( file,
           "\n  // clean up stack\n"
           "  addic  sp, sp, MIXER_STACK_SIZE\n" );

  fprintf( file,
           "\n  // Routine end\n"
           "  blr\n\n" );

  fprintf( file,
           "}\n\n\n" );
}


static void Dump_routine( int op )
{
  Start_routine( op );

  if ( ( ! (op & M_FILTER ) ) || ( op & M_UP_FILTER ) )
  {
    Load_sample( op );

    Apply_volume( op );
  }

  Start_loop( op );

    Filter_with_volume( op ); // if necessary

    Merge_sample( op );

    Add_source( op );

    if ( ( ! (op & M_FILTER ) ) || ( op & M_UP_FILTER ) )
    {
      Load_sample( op );

      Apply_volume( op );

    }

  End_loop( op );

  End_routine( op );
}


void MSS_MAIN_DEF main( int argc, char** argv )
{
  int op;

  if ( argc != 2 )
  {
    file = stdout;
  }
  else
  {
    file = fopen( argv[ 1 ], "wt" );
    if ( file == 0 )
    {
      fprintf( stderr, "Error creating %s.\n", argv[ 1 ] );
      exit( 1 );
    }
  }

  fprintf( file,
           "// defines used by all of the mixer loops\n"
           "#define sample_l      r11\n"
           "#define dest_l        r12\n"
           "#define sample_r      r13\n"
           "#define dest_r        r14\n"
           "#define divider_l     r15\n"
           "#define cur_l1        r16\n"
           "#define cur_r1        r17\n"
           "#define divider_r     r18\n"
           "#define cur_l2        r19\n"
           "#define cur_r2        r20\n\n"
           "#define save_sample_r    0\n"
           "#define save_dest_r      4\n"
           "#define save_divider_l   8\n"
           "#define save_cur_l1     12\n"
           "#define save_cur_r1     16\n"
           "#define save_divider_r  20\n"
           "#define save_cur_l2     24\n"
           "#define save_cur_r2     28\n"
           "#define src_ptr         32\n"
           "#define dest_ptr        36\n"
           "#define src_fract_ptr   40\n\n"
           "#define MIXER_STACK_SIZE 128 // more than we'll need\n\n"
    );

  for( op = 0 ; op < 128 ; op++ )
  {

    if ( op & M_ORDER )  // don't do flipped unless the src is stereo
    {
      if ( ! ( op & M_SRC_STEREO ) )
      {
        continue;
      }
    }

    if ( op & M_FILTER )  // don't do filtering on non-resampled data
    {
      if ( ! ( op & M_RESAMPLE ) )
      {
        continue;
      }
    }

    if ( op & M_FILTER )
    {
      Dump_routine( op | M_UP_FILTER );
    }

    Dump_routine( op );

  }

  // do function table

  fprintf( file,
           "typedef void (AILCALL * MergeLoop ) (\n"
	   "  register S32  FAR * FAR * dest,\n"      //r3
           "  register S32  FAR * dest_end,\n"        //r4
           "  register void const FAR * FAR * src,\n" //r5
           "  register U32  FAR * src_fract,\n"       //r6
           "  register void FAR * src_end,\n"         //r7
           "  register S32  playback_ratio,\n"        //r8
           "  register S32  scale_left,\n"            //r9
           "  register S32  scale_right,\n"           //r10
           "  S32  FAR * cur_l1_ptr,\n"               //stack
           "  S32  FAR * cur_r1_ptr\n"                //stack
           ");\n\n"
           "MergeLoop vector_table[ 128 ] =\n"
           "{\n" );

  for( op = 0 ; op < 128 ; op++ )
  {

    if ( op & M_ORDER )  // don't do flipped unless the src is stereo
    {
      if ( ! ( op & M_SRC_STEREO ) )
      {
        fprintf( file,
                 "  Merge%s,\n", Describe_operation( op & ~M_ORDER ) );
        continue;
      }
    }

    if ( op & M_FILTER )  // don't do filtering on non-resampled data
    {
      if ( ! ( op & M_RESAMPLE ) )
      {
        fprintf( file,
                 "  Merge%s,\n", Describe_operation( op & ~M_FILTER ) );
        continue;
      }
    }

    fprintf( file,
             "  Merge%s,\n", Describe_operation( op ) );
  }
  fprintf( file,
           "};\n\n"
           "// undefine everything\n"
           "#undef sample_l\n"
           "#undef dest_l\n"
           "#undef sample_r\n"
           "#undef dest_r\n"
           "#undef divider_l\n"
           "#undef cur_l1\n"
           "#undef cur_r1\n"
           "#undef divider_r\n"
           "#undef cur_l2\n"
           "#undef cur_r2\n"
           "#undef save_sample_r\n"
           "#undef save_dest_r\n"
           "#undef save_divider_l\n"
           "#undef save_cur_l1\n"
           "#undef save_cur_r1\n"
           "#undef save_divider_r\n"
           "#undef save_cur_l2\n"
           "#undef save_cur_r2\n"
           "#undef src_ptr\n"
           "#undef dest_ptr\n"
           "#undef src_fract_ptr\n"
           "#undef MIXER_STACK_SIZE\n" );
}
