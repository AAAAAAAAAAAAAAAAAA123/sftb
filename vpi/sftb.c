# include	<stdio.h>
//# include	<malloc.h>

# include	<vpi_user.h>
# include	<sndfile.h>

/* This will be the length of the buffer used to hold frames. */
# define		MAX_SIZE	4096

/* Limit to mono files to start with */
# define		MAX_CHAN	32

/* Limit to 32bit integers for now */
# define		MAX_BITS	8*sizeof(int)

/* Default file name */
# define		DEF_FILE	"/dev/zero" // "./data/mono16@22050.f7620.aif"

# define		PRINT(...) vpi_printf("%s: ",func) ; \
				   vpi_printf(__VA_ARGS__)
# define		DBG 0

# if DBG
# define		DEBUG(...) PRINT(__VA_ARGS__)
# else
# define		DEBUG(...)
# endif

# define		ERROR(n)   sf.exit = n ; goto EXIT ;

#define getb(val, bit) (((val & (1 << bit)) >> bit) == 1)
#define Cbit(val, bit)  (val = (val & ~(1 << bit)))
#define Sbit(val, bit)  (val = (val | (1 << bit)))

/* vpiRegArray is not defined in vpi_user.h !!! */

# define vpiRegArray	116
# define vpiNetArray	114

/* There should be one sftb_s struc, but keep it simple for now! */

/* alternative:
 * {
 *
 * file {
 * 	 handle // function argument
 * 	 string // filename string
 * 	 system // file descriptor
 * 	 offset // next seek offset
 * 	 buffer // buffered samples
 * 	}
 *
 * func {
 * 	 exit
 * 	 call
 * 	 scan
 * 	}
 *
 * wire {
 * 	 link // ?
 * 	 mask // bit mask of 32 bits
 * 	 data // the sample buffer
 * 	 type
 * 	 size
 * 	}
 * } */

typedef struct {
SNDFILE *	file;	// file descriptor
SF_INFO		info;	// file metadata
PLI_INT16	exit;
sf_count_t	seek;	// next offset in frames
sf_count_t	read;	// current buffer lenght
vpiHandle	call;
vpiHandle	scan;
PLI_INT32	mask;		
vpiHandle	wire[MAX_CHAN];
PLI_INT32	data[MAX_SIZE];
union {
vpiHandle       handle;	// file path argument
const char *	string;
} name ;
} s_sftb_misc ;

static s_sftb_misc 	sf ;
static s_vpi_value	tb ;

/* Pointer to the buffer, will allocate it for a small file. */

/*
 * Retrieving values from Verilog
 *
 * s_vpi_value v;
 * v.format = vpiIntVal;
 * vpi_get_value (handle, &v);
 *
 * value is in v.value.integer
 *
 * Writing values to Verilog
 * 
 * s_vpi_value v;
 * v.format = vpiIntVal;
 * v.value.integer = 0x1234;
 * vpi_put_value (handle, &v, NULL, vpiNoDelay);
 */

static PLI_INT32 sftb_open_input_file_calltf (char *func) ;
static PLI_INT32 sftb_open_output_file_calltf (char *func) ;
static PLI_INT32 sftb_close_input_file_calltf (char *func) ;
static PLI_INT32 sftb_close_output_file_calltf (char *func) ;

/* Fetch Audio Data from SNDFILE */
static PLI_INT32 sftb_fetch_sample_calltf (char *func) ;
/* Store Audio Data into SNDFILE */
static PLI_INT32 sftb_store_sample_calltf (char *func) ;

static PLI_INT16 sftb_count_arguments (char *func);

void sftb_register()
{
      s_vpi_systf_data tf_data;

      tf_data.type      = vpiSysTask;
      tf_data.tfname    = "$sftb_open_input_file";
      tf_data.calltf    = sftb_open_input_file_calltf;
      tf_data.compiletf = 0;
      tf_data.sizetf    = 0;
      tf_data.user_data = "$sftb_open_input_file";

      vpi_register_systf(&tf_data);

      tf_data.type      = vpiSysTask;
      tf_data.tfname    = "$sftb_open_output_file";
      tf_data.calltf    = sftb_open_output_file_calltf;
      tf_data.compiletf = 0;
      tf_data.sizetf    = 0;
      tf_data.user_data = "$sftb_open_output_file";

      vpi_register_systf(&tf_data);

      tf_data.type      = vpiSysTask;
      tf_data.tfname    = "$sftb_close_input_file";
      tf_data.calltf    = sftb_close_input_file_calltf;
      tf_data.compiletf = 0;
      tf_data.sizetf    = 0;
      tf_data.user_data = "$sftb_close_input_file";

      vpi_register_systf(&tf_data);

      tf_data.type      = vpiSysTask;
      tf_data.tfname    = "$sftb_close_output_file";
      tf_data.calltf    = sftb_close_output_file_calltf;
      tf_data.compiletf = 0;
      tf_data.sizetf    = 0;
      tf_data.user_data = "$sftb_close_output_file";

      vpi_register_systf(&tf_data);

      tf_data.type      = vpiSysTask;
      tf_data.tfname    = "$sftb_store_sample";
      tf_data.calltf    = sftb_store_sample_calltf;
      tf_data.compiletf = 0;
      tf_data.sizetf    = 0;
      tf_data.user_data = "$sftb_store_sample";

      vpi_register_systf(&tf_data);

      tf_data.type      = vpiSysTask;
      tf_data.tfname    = "$sftb_fetch_sample";
      tf_data.calltf    = sftb_fetch_sample_calltf;
      tf_data.compiletf = 0;
      tf_data.sizetf    = 0;
      tf_data.user_data = "$sftb_fetch_sample";

      vpi_register_systf(&tf_data);
}

void (*vlog_startup_routines[])() = { sftb_register, 0 };

static PLI_INT32
sftb_open_input_file_calltf(char *func)
{

/* Handle the arguments, if none - use DEF_FILE */

  sf.exit = sf.seek = 0 ;

  sf.call = vpi_handle (vpiSysTfCall, NULL) ;

  if ( NULL !=( sf.scan = vpi_iterate (vpiArgument, sf.call) ) ) {
	
    sf.name.handle = vpi_scan (sf.scan) ;

    switch(vpi_get(vpiType, sf.name.handle))
    {
      case vpiConstant:
      case vpiStringConst:
        break;

      default:
        PRINT ("first parameter is not a string.\n") ;
	ERROR(1);
    }


    tb.format = vpiStringVal ;
    vpi_get_value (sf.name.handle, &tb) ;
    vpi_free_object (sf.name.handle) ;
    sf.name.string = tb.value.str ;

  } else {
  
    sf.name.string = DEF_FILE ;
  
    PRINT ("zero parameters supplied.\n") ;
    PRINT ("using default file name '%s'.\n", sf.name.string) ;

  }

  /* Open the audio file, more error checking may be needed */

  if ( !( sf.file = sf_open (sf.name.string, SFM_READ, &(sf.info)) ) )
  {
    PRINT ("cannot open input file '%s'.\n", sf.name.string) ;
    ERROR(2); }

  if (sf.info.channels > MAX_CHAN)
  {
    PRINT ("cannot process %d channel(s).\n", (int)sf.info.channels) ;
    PRINT ("my limit is %d channel(s).\n", MAX_CHAN) ;
    ERROR(2); }

  PRINT ("opened '%s'.\n", sf.name.string) ;
  PRINT ("this file has %d frames.\n", (int)sf.info.frames) ;

  sf.read = sf.seek = 0 ; ERROR(1);

EXIT:

    if (sf.exit == 0 || sf.exit == 1) {
	vpi_free_object (sf.call) ;
	vpi_free_object (sf.scan) ;
    } else if (sf.exit == 2) {
	vpi_free_object (sf.call) ;
    }

   if (sf.exit-1) vpi_control (vpiFinish, 1);

   return sf.exit ;
}

static PLI_INT32
sftb_close_input_file_calltf (char *func)
{
    PRINT ("Closing input file '%s'.\n", sf.name.string) ;
    sf_close (sf.file) ;
    // free (sf.data) ;
    return 0 ;
}


static PLI_INT32
sftb_open_output_file_calltf(char *func)
{
  PRINT ("Not implemented (TODO)!\n") ;
  return 0;
}

static PLI_INT32
sftb_close_output_file_calltf (char *func)
{
  // sf_close (output_file) ;
  PRINT ("Not implemented (TODO)!\n") ;
  // free (sf.data) ;
  return 0 ;
}

static PLI_INT32
sftb_fetch_sample_calltf (char *func)
{ 
    static sf_count_t i = 0 ;
    static PLI_INT16 x = 0 ;
    PLI_INT16 t, s ;

    DEBUG ("fetching sample %d;\n", (int)i) ;

    if ( i == 0 ) {

	    if ( sf.read != 0 ) {

		DEBUG (" :sf_seek(..., %d, ...) =", (int)sf.seek) ;

	    	sf.seek = sf_seek (sf.file, sf.seek, SEEK_SET) ;

		vpi_printf (">> %d;\n", (int)sf.seek) ;

	    }

	    if ( sf.seek != -1 ) {

    	    	sf.read = sf_read_int (sf.file, sf.data, MAX_SIZE) ;

		DEBUG ("buffered %d samples;\n", (int)sf.read) ;

		if ( sf.read < MAX_SIZE ) {
			sf.seek = 0 ;
		} else {
			sf.seek += ( sf.read / sf.info.channels ) ;
		}


	    } else {
		    PRINT ("seek error!\n") ;
		    vpi_control(vpiFinish, 1) ;
		    return -1 ;
	    }
    }


    /* parse arguments (which are names of wires)
     * and, if not 0, and there is a channel for
     * it then the channel value will passed on
     * the given wire, if 0, skip the corresponding
     * channel. If it's a mono sound file and more
     * then one wire argument is give, write the
     * same data to all non-zero wires.
     * If we get four wires and only three channels,
     * the fourth wire will be receive zeros.
    */

    if ( x == 0 ) {

      if ( 0 ==( x = sftb_count_arguments(func) ) ) {
        vpi_control(vpiFinish, 1) ;
        return -1; }
    }

    
    tb.format = vpiIntVal ;
 
    tb.value.integer = sf.data[i] ;

    vpi_put_value (sf.wire[0], &tb, NULL, vpiNoDelay) ;

    //PRINT ("sample value is %d;\n", tb.value.integer) ;

    i = (i + sf.info.channels) % sf.read ;

    return 0 ;
}

static PLI_INT32
sftb_store_sample_calltf (char *func)
{
    PRINT ("Not implemented (TODO)!\n") ;
    return 0 ;
}

static PLI_INT16
sftb_count_arguments (char *func)
{
  static int k, d, s, t ;

  k = sf.mask = 0 ;

  sf.call = vpi_handle (vpiSysTfCall, 0) ;
	
  if ( NULL ==( sf.scan = vpi_iterate (vpiArgument, sf.call) ) )
	{
	  PRINT ("zero parameters supplied.\n") ;
	  vpi_free_object (sf.call) ;
	  vpi_control (vpiFinish, 1) ; 
	  return 0; }

  do { DEBUG ("expecting argument %d.\n", k) ;

	if ( NULL !=( sf.wire[k] = vpi_scan (sf.scan) ) ) {

	
	  t = vpi_get(vpiType, sf.wire[k]) ;
	  s = vpi_get(vpiSize, sf.wire[k]) ;

          DEBUG ("vpiType of sf.wire[%d] is %d.\n", k, t ) ;
          DEBUG ("vpiSize of sf.wire[%d] is %d.\n", k, s ) ;

	  /* TODO: implement checking that Net/Reg names are uniq */

	  switch(t) {
            case vpiNet:
            if (s==(MAX_BITS))
		  Sbit(sf.mask, k) ;
	    else { PRINT ("unsupported size of vpiNet!\n") ;
		   /*Cbit(sf.mask, k);*/ }
		  break;
	    case vpiReg:
	    if (s==(MAX_BITS))
		  Sbit(sf.mask, k) ;
	    else { PRINT ("unsupported size of vpiNet!\n") ;
		   /*Cbit(sf.mask, k);*/ }
		  break;
	    case vpiNetArray:
		  PRINT ("vpiNetArray type is not implemented!\n") ;
		  /*Cbit(sf.mask, k);*/
		  break;
            case vpiRegArray:
		  PRINT ("vpiRegArray type is not implemented!\n") ;
		  /*Cbit(sf.mask, k);*/
		  break;
	    default:
		  /*Cbit(sf.mask, k);*/
		  PRINT ("vpiType %d is not supported!\n", t) ;
		  break;
          }

	/* TODO: invistegate the types :) */

	
	} else { DEBUG ("argument %d is not supplied!\n", k) ;
	         break ;
	}

  } while ( (k = ((++k)%MAX_CHAN)) ) ;
  /* Srick to MAX_CHAN here, but use sf.info.channels
   * in the other fethc/store function */

#if DBG
  DEBUG ( "the number of wires is %d.\n", k) ;
  DEBUG ( "sf.mask = 32'b" );

  for ( d = 0 ; d < MAX_CHAN ; d++ ) {
	vpi_printf ("%d", getb(sf.mask, d)) ;
      } vpi_printf ("\n") ;
#endif

  return k;

}
