Template: xawtv/makedev
Type: boolean
Default: true
Description: Create video4linux (/dev/video*) special files?
Description-es: ¿Crear los ficheros especiales video4linux (/dev/video*)?

Template: xawtv/build-config
Type: boolean
Default: false
Description: Create a default configuration for xawtv?
 You can create a system-wide configuration file
 for xawtv with reasonable default values for the
 country you live in (which TV norm is used for
 example).
 .
 It is not required to have a global configuration
 file, but it will be more comfortable for your
 users if they find a working default configuration.
Description-es: ¿Crear una configuración por defecto para xawtv?
 Puede crear un fichero de configuración para xawtv general para su
 sistema con valores razonables para el país en el que vive (el sistema
 de televisión que se utiliza por ejemplo).
 .
 No es obligatorio tener un fichero de configuración global, pero será
 más cómodo para sus usuarios encontrarse una configuración por defecto
 que funciona.

Template: xawtv/tvnorm
Type: select
Choices: PAL, SECAM, NTSC
Description: Which TV norm is used in your country?
Description-es: ¿Qué sistema de televisión se usa en su país?

Template: xawtv/freqtab
Type: select
Choices: us-bcast, us-cable, us-cable-hrc, japan-bcast, japan-cable, europe-west, europe-east, italy, newzealand, australia, ireland, france, china-bcast
Description: Which frequency table should be used?
 A frequency table is just a list of TV channel
 names/numbers and the corresponding broadcast
 frequencies for these channels.  Different regions
 use different standards here...
Description-es: ¿Que tabla de frecuencias se debería usar?
 Una tabla de frecuencias es sólo una lista de nombres/números de
 canales de televisión y sus correspondientes frecuencias de emisión.
 Diferentes regiones utilizan diferentes estándares para esto...

Template: xawtv/channel-scan
Type: boolean
Default: yes
Description: scan for TV stations?
 I can do a scan of all channels and put a list of
 the TV stations I've found into the config file.
 .
 This requires a working bttv driver.  If bttv isn't
 configured correctly I might not find the TV stations.
 .
 I'll try to pick up the channel names from videotext.
 This will work with PAL only.
Description-es: ¿Buscar cadenas de televisión?
 Puedo realizar una búsqueda de todos los canales y poner una lista de
 las cadenas de televisión encontradas en el fichero de configuración.
 .
 Esto requiere un controlador bttv que funcione. Si bttv no se encuentra
 configurado correctamente puede que no encuentre las cadenas de
 televisión.
 .
 Trataré de obtener los nombres de los canales del teletexto. Esto sólo
 funcionará con PAL.
