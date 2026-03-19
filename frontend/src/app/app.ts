import { Component, ViewChild, ElementRef, signal } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { FormsModule } from '@angular/forms';
import { firstValueFrom } from 'rxjs';

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
  @ViewChild('fileInput') private fileInput!: ElementRef<HTMLInputElement>;

  constructor(private http: HttpClient) {}

  async executeCommand() {
    if (!this.input.trim()) return;
    const cmd = this.input;
    this.input = ''; 
    await this.processSingleCommand(cmd);
  }

  async processSingleCommand(comando: string) {
    this.output.update(texto => texto + comando + '\n');
    this.scrollToBottom();

    try {
      const response = await firstValueFrom(
        this.http.post('http://localhost:8080/api/execute', comando, { responseType: 'text' })
      );
      this.output.update(texto => texto + response + '\n> ');
    } catch (err) {
      console.error(err);
      this.output.update(texto => texto + 'Error de conexión con el servidor C++\n> ');
    }
    
    this.scrollToBottom();
  }

  triggerFileInput() {
    this.fileInput.nativeElement.click();
  }

  async onFileSelected(event: any) {
    const file: File = event.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = async (e) => {
      const text = e.target?.result as string;
      if (!text) return;

      const lineas = text.split('\n');
      this.output.update(texto => texto + `[SCRIPT] Iniciando ejecución de: ${file.name}...\n> `);

      for (let linea of lineas) {
        linea = linea.trim();

        const comandoReal = linea.split('#')[0].trim();

        if (!comandoReal) {
          continue;
        }

        await this.processSingleCommand(comandoReal);
      }
      
      this.output.update(texto => texto + `[SCRIPT] Ejecución finalizada.\n> `);
      this.scrollToBottom();
      
      this.fileInput.nativeElement.value = '';
    };

    reader.readAsText(file);
  }

  scrollToBottom(): void {
    setTimeout(() => {
      try {
        this.myScrollContainer.nativeElement.scrollTop = this.myScrollContainer.nativeElement.scrollHeight;
      } catch(err) { }
    }, 50); 
  }
}