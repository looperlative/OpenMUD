#define LOCKER_MAX_NAME_LENGTH	20
#define MAX_LOCKER_GUESTS	10
#define MAX_LOCKERS		500
#define MAX_LOCKER_ITEMS	1024	/* hard upper bound for file read buffer */


struct locker_control_rec {
   char  name[LOCKER_MAX_NAME_LENGTH + 1];	/* lowercase letters+digits	*/
   long  owner;					/* idnum of primary owner	*/
   int   num_of_guests;
   long  guests[MAX_LOCKER_GUESTS];		/* idnums of guests		*/
   time_t created_on;
   long  spare[8];
};


void	Locker_boot(void);
void	Locker_save_control(void);
int	find_locker_by_name(const char *name);
int	find_locker_owned_by(long idnum);
int	count_lockers_shared_to(long idnum);
int	Locker_can_access(struct char_data *ch, int idx);
int	Locker_valid_name(const char *name);
void	Locker_list_contents(struct char_data *ch, int idx);
ACMD(do_locker);
ACMD(do_lcontrol);
