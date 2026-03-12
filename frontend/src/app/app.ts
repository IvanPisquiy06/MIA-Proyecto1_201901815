// 1. Importamos 'signal'
import { Component, ViewChild, ElementRef, signal } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './app.html',
  styleUrl: './app.scss'
})
export class App {
  title = 'Consola MIA';
  input: string = '';
  
  output = signal('Bienvenido a C++ DISK Web. Servidor listo...\n> ');

  @ViewChild('scrollMe') private myScrollContainer!: ElementRef;

  constructor(private http: HttpClient) {}

  executeCommand() {
    if (!this.input.trim()) return;

    const comandoAEnviar = this.input;
    
    // 3. Para actualizar un signal usamos .update()
    this.output.update(textoAnterior => textoAnterior + comandoAEnviar + '\n');
    this.input = ''; 
    this.scrollToBottom();

    this.http.post('http://localhost:8080/api/execute', comandoAEnviar, { responseType: 'text' })
      .subscribe({
        next: (response) => {
          this.output.update(textoAnterior => textoAnterior + response + '\n> ');
          this.scrollToBottom();
        },
        error: (err) => {
          console.error(err);
          this.output.update(textoAnterior => textoAnterior + 'Error de conexión con el servidor C++\n> ');
          this.scrollToBottom();
        }
      });
  }

  scrollToBottom(): void {
    setTimeout(() => {
      try {
        this.myScrollContainer.nativeElement.scrollTop = this.myScrollContainer.nativeElement.scrollHeight;
      } catch(err) { }
    }, 50); 
  }
}