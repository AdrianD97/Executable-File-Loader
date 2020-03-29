Nume: Stefan Adrian-Daniel
Grupa: 334CA

Tema 3 - Loader de executabile

Punctul central al implementarii(care de astfel reprezinta si task-ul efectiv care ne-a fost asignat)
este handler-ul de tratare a exceptiei generate de un acces invalid la memorie(pagina nu a fost alocata
sau nu are permisiunile necesare). Asadar, voi descrie implementarea handler-ului.
Handler-ul este inregistrat de functia record_sigsegv_sig_handler(...) care este apelata in momentul
initializarii loader-ului. Ideea pe care m-am bazat in implementarea handler-ului a fost sa retin in
vectorul data(asociat fiecarui segment) starea fiecarei pagini(mapata sau nemapata).
Pentru fiecare execptie, determin segmentul din care face parte pagina care contine adresa care
a generat-o. In cazul in care pagina nu face parte din nici-un segment, se incearca transferarea
exceptiei pentru a fi tratata de catre un alt handler.
Initial(pana cand se primea prima exceptie pentru un segment(prima pagina din segment pentru care
primeam excpetie)) pointer-ul data era NULL, dupa care, la primul acces invalid de memorie din segment,
determin numarul de pagini si marchez toate paginile ca fiind nemapate(fiecare element din array-ul
data(numar elemente = numar pagini segment) are valoarea 0 initial). In cazul in care, pagina care contine adresa
care a generat exceptia, se gaseste intr-un segment pentru care array-ul data a fost alocat, si pagina
a fost deja mapata, atunci inseamnca ca pagina nu are permisiunile necesare, iar in acest caz se incearca
transferarea exceptiei pentru a fi tratata de catre un alt handler. In schimb daca pagina nu a fost mapata,
atunci aloc memorie pentru pagina si citesc datele paginii din fisier (daca pagina are date in fisier).
Dupa citirea datelor, pagina este marcata ca fiind mapata si semnalizam ca exceptia a fost tratata si
se poate continua executia.

Sa nu uit sa mentionez: foarte interesanta tema.

Compilare:
	nmake -> compileaza biblioteca dinamica so_loader.dll

Git
	https://github.com/AdrianD97/Executable-Loader -> momentan repo-ul este privat, dar 
	va deveni public dupa deadline-ul hard.
