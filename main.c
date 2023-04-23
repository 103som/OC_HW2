#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h>

#define MAX_BOOKS 10
#define MAX_READERS 5

typedef struct {
    int book_id;
    int days_remaining;
    sem_t book_sem;
    pthread_mutex_t mutex;
} Book;

typedef struct {
    int num_books;
    Book books[MAX_BOOKS];
    sem_t mutex;
} Library;

Library* library; // Глобальная переменная для доступа к библиотеке

void librarian_process() {
    printf("Библиотекарь: Библиотека открыта\n");

    while (1) {
        sem_wait(&(library->mutex));
        int available_books = 0;
        for (int i = 0; i < library->num_books; i++) {
            if (library->books[i].days_remaining == 0) {
                available_books++;
                library->books[i].days_remaining = 3;
            }
        }
        sem_post(&(library->mutex));

        if (available_books > 0) {
            printf("Библиотекарь: В наличии %d книг(и), жду читателей...\n", available_books);
        }

        sleep(1);
    }
}

void* reader_process(void* arg) {
    int reader_id = *(int*)arg;
    free(arg);

    printf("Читатель %d: Захожу в библиотеку\n", reader_id);

    while (1) {
        int book_index = rand() % library->num_books;
        Book* book = &(library->books[book_index]);

        pthread_mutex_lock(&(book->mutex));
        sem_wait(&(library->mutex));
        int days_remaining = book->days_remaining;
        if (days_remaining > 0) {
            book->days_remaining--;
        }
        sem_post(&(library->mutex));
        pthread_mutex_unlock(&(book->mutex));

        if (days_remaining == 0) {
            printf("Читатель %d: Беру книгу %d на чтение, осталось дней: %d\n", reader_id, book->book_id, days_remaining);
            sleep(1);

            pthread_mutex_lock(&(book->mutex));
            sem_wait(&(library->mutex));
            book->days_remaining = 3;
            sem_post(&(library->mutex));
            pthread_mutex_unlock(&(book->mutex));
        }

        sleep(1);
    }

    return NULL;
}

int main() {
    // Создание разделяемой памяти для библиотеки
    int fd = shm_open("/library_shm", O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("Ошибка при создании разделяемой памяти");
        exit(EXIT_FAILURE);
    }
    ftruncate(fd, sizeof(Library));
    library = mmap(NULL, sizeof(Library), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (library == MAP_FAILED) {
        perror("Ошибка при отображении разделяемой памяти");
        exit(EXIT_FAILURE);
    }
    close(fd);

    library->num_books = 3;

    for (int i = 0; i < library->num_books; i++) {
        Book* book = &(library->books[i]);
        book->book_id = i + 1;
        book->days_remaining = 3;
        sem_init(&(book->book_sem), 1, 1);
        pthread_mutex_init(&(book->mutex), NULL);
    }

    sem_init(&(library->mutex), 1, 1);

    pthread_t librarian_thread;
    if (pthread_create(&librarian_thread, NULL, (void*)librarian_process, NULL) != 0) {
        perror("Ошибка при создании потока библиотекаря");
        exit(EXIT_FAILURE);
    }

    pthread_t reader_threads[MAX_READERS];
    for (int i = 0; i < MAX_READERS; i++) {
        int* reader_id = malloc(sizeof(int));
        *reader_id = i + 1;
        if (pthread_create(&reader_threads[i], NULL, reader_process, reader_id) != 0) {
            perror("Ошибка при создании потока читателя");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < MAX_READERS; i++) {
        if (pthread_join(reader_threads[i], NULL) != 0) {
            perror("Ошибка при ожидании завершения потока читателя");
            exit(EXIT_FAILURE);
        }
    }

    munmap(library, sizeof(Library));
    shm_unlink("/library_shm");
    sem_destroy(&(library->mutex));

    for (int i = 0; i < library->num_books; i++) {
        Book* book = &(library->books[i]);
        sem_destroy(&(book->book_sem));
        pthread_mutex_destroy(&(book->mutex));
    }

    pthread_cancel(librarian_thread);
    pthread_join(librarian_thread, NULL);

    return 0;
}

