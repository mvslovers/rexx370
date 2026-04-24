/* ------------------------------------------------------------------ */
/*  irx.h - REXX/370 Control Block Definitions                        */
/*                                                                    */
/*  IBM TSO/E V2 REXX compatible control blocks (SC28-1883-0)         */
/*  Based on mvslovers/brexx370 irx.h, cleaned and extended           */
/*                                                                    */
/*  All structures match the IBM-documented layouts byte-for-byte     */
/*  to ensure interface compatibility with existing REXX programs     */
/*  and tools that reference these control blocks.                    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRX_H
#define IRX_H

/* ================================================================== */
/*  Argument Table (ARGTABLE)                                         */
/*  Used by IRXEXEC to pass arguments to a REXX exec                  */
/* ================================================================== */

struct argtable_entry
{
    void *argstring_ptr;  /* Address of the argument string */
    int argstring_length; /* Length of the argument string  */
};

/* End-of-table marker: 8 bytes of 0xFF */
struct argstring
{
    unsigned char argtable_end[8]; /* End of ARGTABLE marker         */
};

#define ARGTABLE_END "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"

/* ================================================================== */
/*  Environment Block (ENVBLOCK)                                      */
/*  Anchor for a Language Processor Environment                       */
/*  Ref: SC28-1883-0, Chapter 14, Page 323                           */
/* ================================================================== */

struct envblock
{
    unsigned char envblock_id[8];      /* Eye-catcher: 'ENVBLOCK'        */
    unsigned char envblock_version[4]; /* Version number in EBCDIC       */
    int envblock_length;               /* Length of ENVBLOCK              */
    void *envblock_parmblock;          /* -> PARMBLOCK                   */
    void *envblock_userfield;          /* User field (available to exits)*/
    void *envblock_workblok_ext;       /* -> Work Block Extension      */
    void *envblock_irxexte;            /* -> IRXEXTE (Entry Point Vector)*/

    /* Error information area */
    union
    {
        unsigned char _envblock_error[256];

        struct
        {
            void *_error_call_;                       /* -> routine in error    */
            int _filler1;                             /* reserved               */
            unsigned char _error_msgid[8];            /* Message ID of error    */
            unsigned char _primary_error_message[80]; /* Primary error message  */
            unsigned char _alternate_error_msg[160];  /* Extended error message */
        } _envblock_struct1;
    } _envblock_union1;

    void *envblock_compgmtb;         /* -> Compiler Programming Table  */
    void *envblock_attnrout_parmptr; /* -> Attention routine parms*/
    void *envblock_ectptr;           /* -> ECT (TSO environments)      */

    /* Information flags */
    union
    {
        unsigned char _envblock_info_flags[4];

        struct
        {
            int _envblock_terma_cleanup : 1, /* IRXTERMA active */
                : 7;
            unsigned char _filler2[3];
        } _envblock_struct2;
    } _envblock_union2;

    int _filler3[4]; /* +304..+319 reserved for rexx370 future use     */
};

#define ENVBLOCK_ID           "ENVBLOCK"
#define ENVBLOCK_VERSION_0100 "0100" /* IBM TSO/E v2 */
#define ENVBLOCK_VERSION_0042 "0042" /* rexx370 deviation (CON-4) */

/* Accessor macros */
#define envblock_error         _envblock_union1._envblock_error
#define error_call_            _envblock_union1._envblock_struct1._error_call_
#define error_msgid            _envblock_union1._envblock_struct1._error_msgid
#define primary_error_message  _envblock_union1._envblock_struct1._primary_error_message
#define alternate_error_msg    _envblock_union1._envblock_struct1._alternate_error_msg
#define envblock_info_flags    _envblock_union2._envblock_info_flags
#define envblock_terma_cleanup _envblock_union2._envblock_struct2._envblock_terma_cleanup

/* ================================================================== */
/*  Evaluation Block (EVALBLOCK)                                      */
/*  Returns a result from IRXEXEC or a function/subroutine            */
/*  Ref: SC28-1883-0, Chapter 12, Page 225                           */
/* ================================================================== */

struct evalblock
{
    int evalblock_evpad1;              /* reserved - set to binary zero  */
    int evalblock_evsize;              /* Size in double words           */
    int evalblock_evlen;               /* Length of data in EVDATA       */
    int evalblock_evpad2;              /* reserved - set to binary zero  */
    unsigned char evalblock_evdata[1]; /* Result data (variable length)  */
};

#define EVALBLOCK_NORESULT   ((int)0x80000000)
#define EVALBLOCK_HEADER_LEN 16
#define EVALBLOCK_DATA_LEN   256

/* ================================================================== */
/*  Exec Block (EXECBLK)                                              */
/*  Identifies the exec to be run by IRXEXEC                          */
/*  Ref: SC28-1883-0, Chapter 12, Page 220                           */
/* ================================================================== */

struct execblk
{
    unsigned char exec_blk_acryn[8]; /* Acronym: 'IRXEXECB'           */
    int exec_blk_length;             /* Length of EXECBLK in bytes     */
    int _filler1;                    /* reserved                       */
    unsigned char exec_member[8];    /* Member name (blank-padded)     */
    unsigned char exec_ddname[8];    /* DD name (blank-padded)         */
    unsigned char exec_subcom[8];    /* Initial subcommand environment */
    void *exec_dsnptr;               /* -> data set name (optional)    */
    int exec_dsnlen;                 /* Length of DSN                  */
    /* --- V1 ends here (0x30) --- */
    void *exec_extname_ptr; /* -> extended exec name          */
    int exec_extname_len;   /* Length of extended name         */
    int _filler2[2];        /* reserved                       */
    /* --- V2 ends here (0x40) --- */
};

#define EXECBLK_ID     "IRXEXECB"
#define EXECBLK_V1_LEN 0x30
#define EXECBLK_V2_LEN 0x40

/* ================================================================== */
/*  In-Storage Control Block (INSTBLK)                                */
/*  Passes a REXX exec as in-storage block to IRXEXEC                 */
/*  Ref: SC28-1883-0, Chapter 12, Page 222                           */
/* ================================================================== */

struct instblk_entry
{
    void *instblk_stmt_; /* Address of REXX statement      */
    int instblk_stmtlen; /* Length of the REXX statement   */
};

struct instblk
{
    /* 128-byte fixed header */
    unsigned char instblk_acronym[8]; /* Eye-catcher: 'IRXINSTB'        */
    int instblk_hdrlen;               /* Length of header (128)          */
    int _filler1;                     /* reserved                        */
    void *instblk_address;            /* -> first INSTBLK_ENTRY          */
    int instblk_usedlen;              /* Total used length of entries    */
    unsigned char instblk_member[8];  /* Member name (for PARSE SOURCE)  */
    unsigned char instblk_ddname[8];  /* DD name (for PARSE SOURCE)      */
    unsigned char instblk_subcom[8];  /* Initial subcommand environment  */
    int _filler2;                     /* reserved                        */
    int instblk_dsnlen;               /* Length of data set name          */
    unsigned char instblk_dsname[54]; /* Data set name                   */
    short int _filler3;               /* reserved                        */
    void *instblk_extname_ptr;        /* -> extended exec name           */
    int instblk_extname_len;          /* Length of extended name          */
    int _filler4[2];                  /* reserved                        */
    /* Entries follow at offset 128 */
};

#define INSTBLK_ID     "IRXINSTB"
#define INSTBLK_HDRLEN 128

/* ================================================================== */
/*  REXX Vector of External Entry Points (IRXEXTE)                    */
/*  Function pointer table for all replaceable routines and services  */
/*  Ref: SC28-1883-0, Chapter 14, Page 328                           */
/* ================================================================== */

struct irxexte
{
    int irxexte_entry_count; /* Number of entry points         */
    void *irxinit;           /* IRXINIT - Env Initialize       */
    void *load_routine;      /* Active Exec Load Routine       */
    void *irxload;           /* IRXLOAD - Default Exec Load    */
    void *irxexcom;          /* IRXEXCOM - Variable Access     */
    void *irxexec;           /* IRXEXEC - Run Exec             */
    void *io_routine;        /* Active I/O Routine             */
    void *irxinout;          /* IRXINOUT - Default I/O         */
    void *irxjcl;            /* IRXJCL - JCL Entry Point       */
    void *irxrlt;            /* IRXRLT - Get Result            */
    void *stack_routine;     /* Active Data Stack Routine      */
    void *irxstk;            /* IRXSTK - Default Data Stack    */
    void *irxsubcm;          /* IRXSUBCM - Subcommand Table    */
    void *irxterm;           /* IRXTERM - Env Terminate        */
    void *irxic;             /* IRXIC - Immediate Commands     */
    void *msgid_routine;     /* Active Message ID Routine      */
    void *irxmsgid;          /* IRXMSGID - Default Msg ID      */
    void *userid_routine;    /* Active User ID Routine         */
    void *irxuid;            /* IRXUID - Default User ID       */
    void *irxterma;          /* IRXTERMA - Abnormal Term       */
    void *irxsay;            /* IRXSAY - SAY Routine           */
    void *irxers;            /* IRXERS - External Routine Srch */
    void *irxhst;            /* IRXHST - Host Command          */
    void *irxhlt;            /* IRXHLT - Halt Condition        */
    void *irxtxt;            /* IRXTXT - Trace Text            */
    void *irxlin;            /* IRXLIN - LINESIZE              */
    void *irxrte;            /* IRXRTE - IRXEXEC Exit          */
};

#define IRXEXTE_ENTRY_COUNT 26

/* ================================================================== */
/*  Shared Variable Block (SHVBLOCK)                                  */
/*  Used by IRXEXCOM for variable access                              */
/*  Ref: SC28-1883-0, Chapter 12, Page 241                           */
/* ================================================================== */

struct shvblock
{
    void *shvnext;         /* -> next SHVBLOCK (or NULL)     */
    int shvuser;           /* User field (FETCH NEXT cursor) */
    unsigned char shvcode; /* Function code                  */
    unsigned char shvret;  /* Return code flags              */
    short int _shvpad;     /* reserved (should be 0)         */
    int shvbufl;           /* Length of fetch value buffer    */
    void *shvnama;         /* -> variable name               */
    int shvnaml;           /* Length of variable name buffer  */
    void *shvvala;         /* -> value buffer                */
    int shvvall;           /* Length of value buffer          */
    int shvnamelen;        /* Actual length of var name       */
    int shvvalelen;        /* Actual length of value          */
};

/* Function codes (SHVCODE) */
#define SHVSTORE 'S'
#define SHVFETCH 'F'
#define SHVDROPV 'D'
#define SHVSYSET 's'
#define SHVSYFET 'f'
#define SHVSYDRO 'd'
#define SHVNEXTV 'N'
#define SHVPRIV  'P'

/* Return code flags (SHVRET) - can be ORed */
#define SHVCLEAN 0x00
#define SHVNEWV  0x01
#define SHVLVAR  0x02
#define SHVTRUNC 0x04
#define SHVBADN  0x08
#define SHVBADV  0x10
#define SHVBADF  0x80

/* IRXEXCOM return codes */
#define SHVRCOK  0
#define SHVRCINV (-1)
#define SHVRCIST (-2)

#define SHVBLEN 0x28 /* 40 bytes */

/* ================================================================== */
/*  Parameter Block (PARMBLOCK)                                       */
/*  Defines characteristics of a Language Processor Environment       */
/*  Ref: SC28-1883-0, Chapter 14, Page 324                           */
/* ================================================================== */

struct parmblock
{
    unsigned char parmblock_id[8];       /* Eye-catcher: 'IRXPARMS'    */
    unsigned char parmblock_version[4];  /* Version in EBCDIC          */
    unsigned char parmblock_language[3]; /* Language identifier         */
    unsigned char _filler1;

    void *parmblock_modnamet;            /* -> MODNAMET                */
    void *parmblock_subcomtb;            /* -> SUBCOMTB header         */
    void *parmblock_packtb;              /* -> PACKTB header           */
    unsigned char parmblock_parsetok[8]; /* Parse source token         */

    /* Flags */
    union
    {
        unsigned char _parmblock_flags[4];

        struct
        {
            int _tsofl : 1, : 1,
                _cmdsofl : 1,
                _funcsofl : 1,
                _nostkfl : 1,
                _noreadfl : 1,
                _nowrtfl : 1,
                _newstkfl : 1;
            int _userpkfl : 1,
                _locpkfl : 1,
                _syspkfl : 1,
                _newscfl : 1,
                _closexfl : 1,
                _noestae : 1,
                _rentrant : 1,
                _nopmsgs : 1;
            int _altmsgs : 1,
                _spshare : 1,
                _storfl : 1,
                _noloaddd : 1,
                _nomsgwto : 1,
                _nomsgio : 1,
                _rostorfl : 1, : 1;
            unsigned char _filler2;
        } _parmblock_struct1;
    } _parmblock_union1;

    /* Masks (correspond 1:1 to flags) */
    union
    {
        unsigned char _parmblock_masks[4];

        struct
        {
            int _tsofl_mask : 1, : 1,
                _cmdsofl_mask : 1,
                _funcsofl_mask : 1,
                _nostkfl_mask : 1,
                _noreadfl_mask : 1,
                _nowrtfl_mask : 1,
                _newstkfl_mask : 1;
            int _userpkfl_mask : 1,
                _locpkfl_mask : 1,
                _syspkfl_mask : 1,
                _newscfl_mask : 1,
                _closexfl_mask : 1,
                _noestae_mask : 1,
                _rentrant_mask : 1,
                _nopmsgs_mask : 1;
            int _altmsgs_mask : 1,
                _spshare_mask : 1,
                _storfl_mask : 1,
                _noloaddd_mask : 1,
                _nomsgwto_mask : 1,
                _nomsgio_mask : 1,
                _rostorfl_mask : 1, : 1;
            unsigned char _filler3;
        } _parmblock_struct2;
    } _parmblock_union2;

    int parmblock_subpool;              /* Subpool number             */
    unsigned char parmblock_addrspn[8]; /* Address space name         */
    unsigned char parmblock_ffff[8];    /* End marker (hex FF)        */
};

#define PARMBLOCK_ID           "IRXPARMS"
#define PARMBLOCK_VERSION_0200 "0200" /* IBM TSO/E v2 */
#define PARMBLOCK_VERSION_0042 "0042" /* rexx370 deviation (CON-4) */

/* Flag accessor macros */
#define parmblock_flags _parmblock_union1._parmblock_flags
#define tsofl           _parmblock_union1._parmblock_struct1._tsofl
#define cmdsofl         _parmblock_union1._parmblock_struct1._cmdsofl
#define funcsofl        _parmblock_union1._parmblock_struct1._funcsofl
#define nostkfl         _parmblock_union1._parmblock_struct1._nostkfl
#define noreadfl        _parmblock_union1._parmblock_struct1._noreadfl
#define nowrtfl         _parmblock_union1._parmblock_struct1._nowrtfl
#define newstkfl        _parmblock_union1._parmblock_struct1._newstkfl
#define userpkfl        _parmblock_union1._parmblock_struct1._userpkfl
#define locpkfl         _parmblock_union1._parmblock_struct1._locpkfl
#define syspkfl         _parmblock_union1._parmblock_struct1._syspkfl
#define newscfl         _parmblock_union1._parmblock_struct1._newscfl
#define closexfl        _parmblock_union1._parmblock_struct1._closexfl
#define noestae         _parmblock_union1._parmblock_struct1._noestae
#define rentrant        _parmblock_union1._parmblock_struct1._rentrant
#define nopmsgs         _parmblock_union1._parmblock_struct1._nopmsgs
#define altmsgs         _parmblock_union1._parmblock_struct1._altmsgs
#define spshare         _parmblock_union1._parmblock_struct1._spshare
#define storfl          _parmblock_union1._parmblock_struct1._storfl
#define noloaddd        _parmblock_union1._parmblock_struct1._noloaddd
#define nomsgwto        _parmblock_union1._parmblock_struct1._nomsgwto
#define nomsgio         _parmblock_union1._parmblock_struct1._nomsgio
#define rostorfl        _parmblock_union1._parmblock_struct1._rostorfl

/* Mask accessor macros */
#define parmblock_masks _parmblock_union2._parmblock_masks
#define tsofl_mask      _parmblock_union2._parmblock_struct2._tsofl_mask
#define cmdsofl_mask    _parmblock_union2._parmblock_struct2._cmdsofl_mask
#define funcsofl_mask   _parmblock_union2._parmblock_struct2._funcsofl_mask
#define nostkfl_mask    _parmblock_union2._parmblock_struct2._nostkfl_mask
#define noreadfl_mask   _parmblock_union2._parmblock_struct2._noreadfl_mask
#define nowrtfl_mask    _parmblock_union2._parmblock_struct2._nowrtfl_mask
#define newstkfl_mask   _parmblock_union2._parmblock_struct2._newstkfl_mask
#define userpkfl_mask   _parmblock_union2._parmblock_struct2._userpkfl_mask
#define locpkfl_mask    _parmblock_union2._parmblock_struct2._locpkfl_mask
#define syspkfl_mask    _parmblock_union2._parmblock_struct2._syspkfl_mask
#define newscfl_mask    _parmblock_union2._parmblock_struct2._newscfl_mask
#define closexfl_mask   _parmblock_union2._parmblock_struct2._closexfl_mask
#define noestae_mask    _parmblock_union2._parmblock_struct2._noestae_mask
#define rentrant_mask   _parmblock_union2._parmblock_struct2._rentrant_mask
#define nopmsgs_mask    _parmblock_union2._parmblock_struct2._nopmsgs_mask
#define altmsgs_mask    _parmblock_union2._parmblock_struct2._altmsgs_mask
#define spshare_mask    _parmblock_union2._parmblock_struct2._spshare_mask
#define storfl_mask     _parmblock_union2._parmblock_struct2._storfl_mask
#define noloaddd_mask   _parmblock_union2._parmblock_struct2._noloaddd_mask
#define nomsgwto_mask   _parmblock_union2._parmblock_struct2._nomsgwto_mask
#define nomsgio_mask    _parmblock_union2._parmblock_struct2._nomsgio_mask
#define rostorfl_mask   _parmblock_union2._parmblock_struct2._rostorfl_mask

/* ================================================================== */
/*  Module Name Table (MODNAMET)                                      */
/*  Ref: SC28-1883-0, Chapter 14, Page 330                           */
/* ================================================================== */

struct modnamet
{
    unsigned char modnamet_indd[8];     /* Input DD (SYSTSIN)             */
    unsigned char modnamet_outdd[8];    /* Output DD (SYSTSPRT)           */
    unsigned char modnamet_loaddd[8];   /* Load exec DD (SYSEXEC)         */
    unsigned char modnamet_iorout[8];   /* I/O routine                    */
    unsigned char modnamet_exrout[8];   /* Exec load routine              */
    unsigned char modnamet_getfreer[8]; /* Storage management routine     */
    unsigned char modnamet_execinit[8]; /* Exec initialization exit       */
    unsigned char modnamet_attnrout[8]; /* Attention handling routine     */
    unsigned char modnamet_stackrt[8];  /* Data stack routine             */
    unsigned char modnamet_irxexecx[8]; /* IRXEXEC exit routine           */
    unsigned char modnamet_idrout[8];   /* User ID routine                */
    unsigned char modnamet_msgidrt[8];  /* Message ID routine             */
    unsigned char modnamet_execterm[8]; /* Exec termination exit          */
    unsigned char modnamet_ffff[8];     /* End marker (hex FF)            */
};

/* ================================================================== */
/*  Subcommand Table (SUBCOMTB)                                       */
/*  Ref: SC28-1883-0, Chapter 14                                     */
/* ================================================================== */

struct subcomtb_header
{
    void *subcomtb_first;              /* -> first SUBCOMTB entry        */
    int subcomtb_total;                /* Total entries                  */
    int subcomtb_used;                 /* Used entries                   */
    int subcomtb_length;               /* Length of each entry           */
    unsigned char subcomtb_initial[8]; /* Initial subcommand env name    */
    unsigned char _filler1[8];         /* reserved                       */
    unsigned char subcomtb_ffff[8];    /* End marker (hex FF)            */
};

struct subcomtb_entry
{
    unsigned char subcomtb_name[8];    /* Subcommand environment name    */
    unsigned char subcomtb_routine[8]; /* Handler routine name           */
    unsigned char subcomtb_token[16];  /* User token                     */
};

#define SUBCOMTB_ENTRY_LEN 32

/* ================================================================== */
/*  Function Package Table (PACKTB)                                   */
/* ================================================================== */

struct packtb_header
{
    void *packtb_user_first; /* -> first user entry            */
    int packtb_user_total;
    int packtb_user_used;
    void *packtb_local_first; /* -> first local entry           */
    int packtb_local_total;
    int packtb_local_used;
    void *packtb_system_first; /* -> first system entry          */
    int packtb_system_total;
    int packtb_system_used;
    int packtb_length;            /* Length of each entry           */
    unsigned char packtb_ffff[8]; /* End marker                     */
};

struct packtb_entry
{
    unsigned char packtb_name[8]; /* Package name                   */
    unsigned char _filler1[8];    /* reserved / next                */
    unsigned char valid_parmblock_version[4];
};

/* ================================================================== */
/*  Function Package Directory (FPCKDIR)                              */
/* ================================================================== */

struct fpckdir_header
{
    unsigned char fpckdir_id[8];
    int fpckdir_header_length;
    int fpckdir_functions;
    int _filler1;
    int fpckdir_entry_length;
};

struct fpckdir_entry
{
    unsigned char fpckdir_funcname[8];
    void *fpckdir_funcaddr;
    int _filler1;
    unsigned char fpckdir_sysname[8];
    unsigned char fpckdir_sysdd[8];
};

/* ================================================================== */
/*  External Function Parameter List (EFPL)                           */
/* ================================================================== */

struct efpl
{
    void *efplcom;  /* reserved                       */
    void *efplbarg; /* reserved                       */
    void *efplearg; /* reserved                       */
    void *efplfb;   /* reserved                       */
    void *efplarg;  /* -> argument table              */
    void *efpleval; /* -> address of EVALBLOCK        */
};

/* ================================================================== */
/*  Work Block Extension (IBM standard)                               */
/*  Ref: SC28-1883-0, Chapter 14, Page 326                           */
/* ================================================================== */

struct workblok_ext
{
    void *workext_execblk;  /* -> EXECBLK                     */
    void *workext_argtable; /* -> first ARGTABLE entry        */

    union
    {
        unsigned char _workext_flags[4];

        struct
        {
            int _workext_command : 1,
                _workext_function : 1,
                _workext_subroutine : 1, : 5;
            unsigned char _filler1[3];
        } _workext_struct1;
    } _workext_union1;

    void *workext_instblk;        /* -> INSTBLK header              */
    void *workext_cpplptr;        /* -> CPPL (TSO only)             */
    void *workext_evalblock;      /* -> user EVALBLOCK              */
    void *workext_workarea;       /* -> workarea header             */
    void *workext_userfield;      /* User field                     */
    int workext_rtproc;           /* Runtime processor word         */
    void *workext_source_address; /* -> source image             */
    int workext_source_length;    /* Source length               */
    int _filler2;                 /* reserved                       */
};

#define workext_flags      _workext_union1._workext_flags
#define workext_command    _workext_union1._workext_struct1._workext_command
#define workext_function   _workext_union1._workext_struct1._workext_function
#define workext_subroutine _workext_union1._workext_struct1._workext_subroutine

/* ================================================================== */
/*  Dataset Information Block (DSIB)                                  */
/* ================================================================== */

struct dsib_info
{
    unsigned char dsib_id[8];
    short int dsib_length;
    short int _filler1;
    unsigned char dsib_ddname[8];

    union
    {
        unsigned char _dsib_flags[4];

        struct
        {
            int _dsib_lrecl_flag : 1,
                _dsib_blksz_flag : 1,
                _dsib_dsorg_flag : 1,
                _dsib_recfm_flag : 1,
                _dsib_get_flag : 1,
                _dsib_put_flag : 1,
                _dsib_mode_flag : 1,
                _dsib_cc_flag : 1;
            int _dsib_trc_flag : 1, : 7;
            unsigned char _filler2[2];
        } _dsib_struct1;
    } _dsib_union1;

    short int dsib_lrecl;
    short int dsib_blksz;
    unsigned char dsib_dsorg[2];
    unsigned char dsib_recfm[2];
    int dsib_get_cnt;
    int dsib_put_cnt;
    unsigned char dsib_io_mode;
    unsigned char dsib_cc;
    unsigned char dsib_trc;
    unsigned char _filler3;
    int _filler4[3];
};

#define DSIB_ID  "IRXDSIB "
#define DSIB_LEN 0x38

/* DSIB flag accessors */
#define dsib_flags      _dsib_union1._dsib_flags
#define dsib_lrecl_flag _dsib_union1._dsib_struct1._dsib_lrecl_flag
#define dsib_blksz_flag _dsib_union1._dsib_struct1._dsib_blksz_flag
#define dsib_dsorg_flag _dsib_union1._dsib_struct1._dsib_dsorg_flag
#define dsib_recfm_flag _dsib_union1._dsib_struct1._dsib_recfm_flag
#define dsib_get_flag   _dsib_union1._dsib_struct1._dsib_get_flag
#define dsib_put_flag   _dsib_union1._dsib_struct1._dsib_put_flag
#define dsib_mode_flag  _dsib_union1._dsib_struct1._dsib_mode_flag
#define dsib_cc_flag    _dsib_union1._dsib_struct1._dsib_cc_flag
#define dsib_trc_flag   _dsib_union1._dsib_struct1._dsib_trc_flag

/* ================================================================== */
/*  Compiler Programming Table (COMPGMTB)                             */
/* ================================================================== */

struct compgmtb_header
{
    void *compgmtb_first;
    int compgmtb_total;
    int compgmtb_used;
    int compgmtb_length;
    unsigned char _filler1[8];
    unsigned char compgmtb_ffff[8];
};

struct compgmtb_entry
{
    unsigned char compgmtb_rtproc[8];
    unsigned char compgmtb_compinit[8];
    unsigned char compgmtb_compterm[8];
    unsigned char compgmtb_compload[8];
    unsigned char compgmtb_compvar[8];
    int compgmtb_storage[4];
};

/* ================================================================== */
/*  Host Command Environment Parameters                               */
/* ================================================================== */

struct irx_hostenv_parms
{
    char *envname;
    char **cmdstring;
    int *cmdlen;
    char **usertoken;
    int *retcode;
};

/* ================================================================== */
/*  IRXEXEC Parameter List (R1 -> plist)                              */
/*  Ref: SC28-1883-0, Chapter 12                                     */
/* ================================================================== */

struct irxexec_plist
{
    void *execblk_ptr;   /* -> EXECBLK (or NULL)           */
    void *arglist_ptr;   /* -> argument table              */
    int flags;           /* Execution flags                */
    void *instblk_ptr;   /* -> INSTBLK (or NULL)           */
    void *cppl_ptr;      /* -> CPPL (TSO, or NULL)         */
    void *evalblk_ptr;   /* -> EVALBLOCK (or NULL)         */
    void *wkarea_ptr;    /* -> work area (or NULL)         */
    void *userfield_ptr; /* -> user field (or NULL)        */
    void *envblock_ptr;  /* -> ENVBLOCK (or NULL)          */
};

/* IRXEXEC flags */
#define IRXEXEC_COMMAND    0x00000000
#define IRXEXEC_FUNCTION   0x20000000
#define IRXEXEC_SUBROUTINE 0x40000000

/* IRXEXEC return codes */
#define IRXEXEC_OK        0
#define IRXEXEC_RCNZ      4
#define IRXEXEC_NOTFOUND  20
#define IRXEXEC_NOENV     28
#define IRXEXEC_BADPLIST  32
#define IRXEXEC_NOHOSTCMD (-3)

#endif /* IRX_H */
