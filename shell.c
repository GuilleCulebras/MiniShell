#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

typedef struct info info;

struct info{
	char cmd[128];
	char *argv[1024];
};

info comandos[64];
int p[128][2];
int pa[2];
char fileIn[1024];
char fileOut[1024];
int pids[128];

int
trocear(char *s, char *delm, char *tokens[]){
	char *aux;
	char *token;
	int n=0;
	token=strtok_r(s,delm,&aux);
	while(token!=NULL){
		tokens[n]=token;
		n++;
		s=aux;
		token=strtok_r(s,delm,&aux);
	}
	return n;
}

void
builtin_cd(int e){
	char *home;

	if(comandos[0].argv[1] != NULL){//cd acompañado de directorio
		e= chdir(comandos[0].argv[1]);//Movemos a este directorio llamada al sistema chdir
		if(e<0)
			fprintf(stderr, "chd: %s: No existe el archivo o directorio\n",comandos[0].argv[1]);
	}
	else{//cd solo volvemos al directorio home
		home= getenv("HOME");
		e= chdir(home);
		if(e<0)
			fprintf(stderr, "chd: %s error\n",comandos[0].argv[1]);
	}
}

void
crear_pipes(int num){
	int i;
	for (i=0; i < (num-1); i++){
		if(pipe(p[i])<0)
			err(1,"Creating pipe");
	}
}

void
cerrar_pipes(int num){
	int i;
	for (i=0; i < (num-1); i++){
		close(p[i][0]);
		close(p[i][1]);
	}
}
	
void 
dir_ok(char *cmd, char *cmdsalida){
	
	char *path;
	char *rutas[64];
	char comando[128];
	int n;
	int i;
	
	memset(cmdsalida,0,128);
	if (access(cmd,X_OK)==0){		
		strcpy(cmdsalida,cmd);
	}else{
		path=getenv("PATH");

		n=trocear(path,":",rutas);
		for(i=0;i<n;i++){
			sprintf(comando,"%s/%s",rutas[i],cmd);
			if (access(comando,X_OK)==0){
					strcpy(cmdsalida,comando);
				
			}	
		}
	}
}

void
hacerbackground(int j, int bckg, int redirIbck){//si metemos un & al final y no hay nada en la entrada estandar, tomamos /dev/null
	int fd;

	if((j==0)&&(bckg)&&(!redirIbck)){

		fd= open("/dev/null",O_RDONLY);
		if(fd<0)
			err(1,"Open");
		dup2(fd,0);
		close(fd);

	}
}

void
redireccionar(int j, int ncomandosred, int redirIred, int redirOred){
	int fd;
	
	if ((j==0)&&(redirIred)){
					
		fd=open(fileIn,O_RDONLY);
		if(fd<0)
			err(1,"Open");
		dup2(fd,0);
		close(fd);
	}
	
	if ((j==(ncomandosred-1))&&(redirOred)){
		fd= creat(fileOut,0644);
		if(fd<0)
			err(1,"Creat");
		dup2(fd,1);
		close(fd);
	}
}

int
asignacion(char *c){
	int i;
	char *var;
	char *val;

	var= c;

	for(i=0;i<(strlen(c)-1);i++){

		if((c[i]=='=')){
			c[i]='\0'; //Elimina el =
			val= &c[i+1];
			val[strlen(val)-1]='\0'; //quitamos el \n
			setenv(var,val,1); //Lo que haya en val se lo asigno a var, sobreescribiendo
			return 1;
		}
		
	}
	return 0;
}


void
mirarVarEntorno(int n, char *argumentos[128]){
	char *c;
	int i;

	for (i=0;i<n;++i){
		if(argumentos[i][0]=='$'){//Si la primera posicion del argumento es $
			
			c= &argumentos[i][1]; //Nos colocamos detras de $
			argumentos[i]=getenv(c);
		}
	}
}

void
pipes(int j,int ncomandospi){
	if(j==0) //Primer comando
		dup2(p[0][1],1);
	else if(j==(ncomandospi-1))//Ultimo comando
		dup2(p[j-1][0],0);
	else {//Comando de enmedio
		dup2(p[j-1][0],0);
		dup2(p[j][1],1);
	}
}
void
ejecutar(int ncomandos, char cmdej[],int redirIej, int redirOej, int bckgej, int arrej){
	int i;
	int pid;
	
	for (i = 0; i < ncomandos; i++){
		pid= fork();

		switch(pid){
			case -1:
				err(1,"fork");
				
			case 0:
				if((i==0) && (arrej)){
					dup2(pa[0],0);
					close(pa[0]);
				}

				hacerbackground(i,bckgej,redirIej);
				redireccionar(i,ncomandos,redirIej,redirOej);

				if (ncomandos>1){
					pipes(i,ncomandos);
					cerrar_pipes(ncomandos);
				}		
				
				dir_ok(comandos[i].cmd,cmdej);
				execv(cmdej,comandos[i].argv);

				fprintf(stderr, "%s: No se encontro la orden\n",comandos[i].cmd);
				exit(EXIT_SUCCESS);

			default:
				pids[i]=pid;

		}
	}
}

void
esperar(int ncomandoswait){
	int i;
	//Si hay & al final no esperamos a que terminen los hijos
	for(i=0; i<ncomandoswait;i++){
		if(waitpid(pids[i],NULL,0)<0){
			err(1,"Wait");
		}
	}
}

int
main(int argc, char *argv[]){
	char buffer[1024];
	char *trozos[1024];
	char *trozos2[1024];
	char *trozos3[1024];
	int i,n,Numcmd,ntokens;
	char cmd[1024];
	int ebuilt=0;
	int redirI=0;
	int redirO=0;
	int background=0;
	int arr=0;
	
	for(;;){

		redirI=0;
		redirO=0;
		background=0;
		arr=0;

		printf("; ");
		if (fgets(buffer,1024,stdin)==0)//Lee de la entrada estandar
				break;


		if(buffer[strlen(buffer)-2]=='&')//Si tenemos & al final de linea
			background=1;

		if(asignacion(buffer))//Si hay una asignacion entramos y terminamos ahi, si no la hay continuamos
			continue;
	
		if (strlen(buffer)==1)
			continue;
		
		ntokens= trocear(buffer,">\n",trozos2);
		if (ntokens>1){
			redirO=1;
			strcpy(buffer,trozos2[0]);
			trocear(trozos2[1]," ",trozos3); //quito el espacio en blanco
			strcpy(fileOut,trozos3[0]);	
		}

		ntokens= trocear(buffer,"<",trozos2); //Separamos cada trozo para ver si tiene redireccion
		if (ntokens>1){ //Si tiene guardamos la parte de la direccion en fileIN
			redirI=1;
			strcpy(buffer,trozos[0]);
			trocear(trozos2[1]," ",trozos3); //quito el espacio en blanco
			strcpy(fileIn,trozos3[0]);
		}

		Numcmd= trocear(buffer,"|\r\n&",trozos);//Devuelve el numero de comandos que le pasemos
		//Guarda en trozos lo que haya en buffer tokenizado

		
		for (i = 0; i < Numcmd; i++){//Quedarnos con el comando
			n= trocear(trozos[i]," ",comandos[i].argv);

			mirarVarEntorno(n,comandos[i].argv);

			comandos[i].argv[n]= NULL;
			strcpy(comandos[i].cmd,comandos[i].argv[0]);// Nos quedamos con la parte del comando del trozo

		}

		if(strcmp(comandos[0].argv[0],"chd")==0){//Si tenemos comando chd, redireccionamos
			builtin_cd(ebuilt);

		}else{
			crear_pipes(Numcmd);

			ejecutar(Numcmd,cmd,redirI,redirO,background,arr);

			cerrar_pipes(Numcmd);

			if(arr){
				close(pa[0]);
			}

			if(background==0)
				esperar(Numcmd);
		}

	}

	exit(EXIT_SUCCESS);
}

